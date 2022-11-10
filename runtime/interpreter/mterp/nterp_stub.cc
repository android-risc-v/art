/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "base/enums.h"
#include "base/locks.h"

#include "nterp.h"

#include "base/quasi_atomic.h"
#include "class_linker-inl.h"
#include "dex/dex_instruction_utils.h"
#include "debugger.h"
#include "entrypoints/entrypoint_utils-inl.h"
#include "interpreter/interpreter_cache-inl.h"
#include "interpreter/interpreter_common.h"
#include "interpreter/shadow_frame-inl.h"
#include "mirror/string-alloc-inl.h"
#include "nterp_helpers.h"

/*
 * Stub definitions for targets without nterp implementations.
 */

namespace art {

class ArtMethod;

namespace interpreter {

bool IsNterpSupported() {
  return false;
}

bool CanRuntimeUseNterp() {
  return false;
}

const void* GetNterpEntryPoint() {
  return nullptr;
}

void CheckNterpAsmConstants() {
}

extern "C" void ExecuteNterpImpl() REQUIRES_SHARED(Locks::mutator_lock_) {
  UNIMPLEMENTED(FATAL);
}

inline void UpdateHotness(ArtMethod* method) REQUIRES_SHARED(Locks::mutator_lock_) {
  // The hotness we will add to a method when we perform a
  // field/method/class/string lookup.
  constexpr uint16_t kNterpHotnessLookup = 0xf;
  method->UpdateCounter(kNterpHotnessLookup);
}

template<typename T>
inline void UpdateCache(Thread* self, const uint16_t* dex_pc_ptr, T value) {
  self->GetInterpreterCache()->Set(self, dex_pc_ptr, value);
}

template<typename T>
inline void UpdateCache(Thread* self, const uint16_t* dex_pc_ptr, T* value) {
  UpdateCache(self, dex_pc_ptr, reinterpret_cast<size_t>(value));
}

static constexpr uint8_t kInvalidInvokeType = 255u;
static_assert(static_cast<uint8_t>(kMaxInvokeType) < kInvalidInvokeType);

static constexpr uint8_t GetOpcodeInvokeType(uint8_t opcode) {
  switch (opcode) {
    case Instruction::INVOKE_DIRECT:
    case Instruction::INVOKE_DIRECT_RANGE:
      return static_cast<uint8_t>(kDirect);
    case Instruction::INVOKE_INTERFACE:
    case Instruction::INVOKE_INTERFACE_RANGE:
      return static_cast<uint8_t>(kInterface);
    case Instruction::INVOKE_STATIC:
    case Instruction::INVOKE_STATIC_RANGE:
      return static_cast<uint8_t>(kStatic);
    case Instruction::INVOKE_SUPER:
    case Instruction::INVOKE_SUPER_RANGE:
      return static_cast<uint8_t>(kSuper);
    case Instruction::INVOKE_VIRTUAL:
    case Instruction::INVOKE_VIRTUAL_RANGE:
      return static_cast<uint8_t>(kVirtual);

    default:
      return kInvalidInvokeType;
  }
}

static constexpr std::array<uint8_t, 256u> GenerateOpcodeInvokeTypes() {
  std::array<uint8_t, 256u> opcode_invoke_types{};
  for (size_t opcode = 0u; opcode != opcode_invoke_types.size(); ++opcode) {
    opcode_invoke_types[opcode] = GetOpcodeInvokeType(opcode);
  }
  return opcode_invoke_types;
}

static constexpr std::array<uint8_t, 256u> kOpcodeInvokeTypes = GenerateOpcodeInvokeTypes();

FLATTEN
extern "C" size_t NterpGetMethod(Thread* self, ArtMethod* caller, const uint16_t* dex_pc_ptr)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  UpdateHotness(caller);
  const Instruction* inst = Instruction::At(dex_pc_ptr);
  Instruction::Code opcode = inst->Opcode();
  DCHECK(IsUint<8>(static_cast<std::underlying_type_t<Instruction::Code>>(opcode)));
  uint8_t raw_invoke_type = kOpcodeInvokeTypes[opcode];
  DCHECK_LE(raw_invoke_type, kMaxInvokeType);
  InvokeType invoke_type = static_cast<InvokeType>(raw_invoke_type);

  // In release mode, this is just a simple load.
  // In debug mode, this checks that we're using the correct instruction format.
  uint16_t method_index =
      (opcode >= Instruction::INVOKE_VIRTUAL_RANGE) ? inst->VRegB_3rc() : inst->VRegB_35c();

  ClassLinker* const class_linker = Runtime::Current()->GetClassLinker();
  ArtMethod* resolved_method = caller->SkipAccessChecks()
      ? class_linker->ResolveMethod<ClassLinker::ResolveMode::kNoChecks>(
            self, method_index, caller, invoke_type)
      : class_linker->ResolveMethod<ClassLinker::ResolveMode::kCheckICCEAndIAE>(
            self, method_index, caller, invoke_type);
  if (resolved_method == nullptr) {
    DCHECK(self->IsExceptionPending());
    return 0;
  }

  if (invoke_type == kSuper) {
    resolved_method = caller->SkipAccessChecks()
        ? FindSuperMethodToCall</*access_check=*/false>(method_index, resolved_method, caller, self)
        : FindSuperMethodToCall</*access_check=*/true>(method_index, resolved_method, caller, self);
    if (resolved_method == nullptr) {
      DCHECK(self->IsExceptionPending());
      return 0;
    }
  }

  if (invoke_type == kInterface) {
    size_t result = 0u;
    if (resolved_method->GetDeclaringClass()->IsObjectClass()) {
      // Set the low bit to notify the interpreter it should do a vtable call.
      DCHECK_LT(resolved_method->GetMethodIndex(), 0x10000);
      result = (resolved_method->GetMethodIndex() << 16) | 1U;
    } else {
      DCHECK(resolved_method->GetDeclaringClass()->IsInterface());
      DCHECK(!resolved_method->IsCopied());
      if (!resolved_method->IsAbstract()) {
        // Set the second bit to notify the interpreter this is a default
        // method.
        result = reinterpret_cast<size_t>(resolved_method) | 2U;
      } else {
        result = reinterpret_cast<size_t>(resolved_method);
      }
    }
    UpdateCache(self, dex_pc_ptr, result);
    return result;
  } else if (resolved_method->IsStringConstructor()) {
    CHECK_NE(invoke_type, kSuper);
    resolved_method = WellKnownClasses::StringInitToStringFactory(resolved_method);
    // Or the result with 1 to notify to nterp this is a string init method. We
    // also don't cache the result as we don't want nterp to have its fast path always
    // check for it, and we expect a lot more regular calls than string init
    // calls.
    return reinterpret_cast<size_t>(resolved_method) | 1;
  } else if (invoke_type == kVirtual) {
    UpdateCache(self, dex_pc_ptr, resolved_method->GetMethodIndex());
    return resolved_method->GetMethodIndex();
  } else {
    UpdateCache(self, dex_pc_ptr, resolved_method);
    return reinterpret_cast<size_t>(resolved_method);
  }
}

extern "C" size_t NterpGetStaticField(Thread* self,
                                      ArtMethod* caller,
                                      const uint16_t* dex_pc_ptr,
                                      size_t resolve_field_type)  // Resolve if not zero
    REQUIRES_SHARED(Locks::mutator_lock_) {
  UpdateHotness(caller);
  const Instruction* inst = Instruction::At(dex_pc_ptr);
  uint16_t field_index = inst->VRegB_21c();
  ClassLinker* const class_linker = Runtime::Current()->GetClassLinker();
  Instruction::Code opcode = inst->Opcode();
  ArtField* resolved_field = ResolveFieldWithAccessChecks(
      self,
      class_linker,
      field_index,
      caller,
      /*is_static=*/ true,
      /*is_put=*/ IsInstructionSPut(opcode),
      resolve_field_type);

  if (resolved_field == nullptr) {
    DCHECK(self->IsExceptionPending());
    return 0;
  }
  if (UNLIKELY(!resolved_field->GetDeclaringClass()->IsVisiblyInitialized())) {
    StackHandleScope<1> hs(self);
    Handle<mirror::Class> h_class(hs.NewHandle(resolved_field->GetDeclaringClass()));
    if (UNLIKELY(!class_linker->EnsureInitialized(
                      self, h_class, /*can_init_fields=*/ true, /*can_init_parents=*/ true))) {
      DCHECK(self->IsExceptionPending());
      return 0;
    }
    DCHECK(h_class->IsInitializing());
  }
  if (resolved_field->IsVolatile()) {
    // Or the result with 1 to notify to nterp this is a volatile field. We
    // also don't cache the result as we don't want nterp to have its fast path always
    // check for it.
    return reinterpret_cast<size_t>(resolved_field) | 1;
  } else {
    // For sput-object, try to resolve the field type even if we were not requested to.
    // Only if the field type is successfully resolved can we update the cache. If we
    // fail to resolve the type, we clear the exception to keep interpreter
    // semantics of not throwing when null is stored.
    if (opcode == Instruction::SPUT_OBJECT &&
        resolve_field_type == 0 &&
        resolved_field->ResolveType() == nullptr) {
      DCHECK(self->IsExceptionPending());
      self->ClearException();
    } else {
      UpdateCache(self, dex_pc_ptr, resolved_field);
    }
    return reinterpret_cast<size_t>(resolved_field);
  }
}

const void* GetNterpWithClinitEntryPoint() {
  UNIMPLEMENTED(FATAL);
  return nullptr;
}

extern "C" uint32_t NterpGetInstanceFieldOffset(Thread* self,
                                                ArtMethod* caller,
                                                const uint16_t* dex_pc_ptr,
                                                size_t resolve_field_type)  // Resolve if not zero
    REQUIRES_SHARED(Locks::mutator_lock_) {
  UpdateHotness(caller);
  const Instruction* inst = Instruction::At(dex_pc_ptr);
  uint16_t field_index = inst->VRegC_22c();
  ClassLinker* const class_linker = Runtime::Current()->GetClassLinker();
  Instruction::Code opcode = inst->Opcode();
  ArtField* resolved_field = ResolveFieldWithAccessChecks(
      self,
      class_linker,
      field_index,
      caller,
      /*is_static=*/ false,
      /*is_put=*/ IsInstructionIPut(opcode),
      resolve_field_type);
  if (resolved_field == nullptr) {
    DCHECK(self->IsExceptionPending());
    return 0;
  }
  if (resolved_field->IsVolatile()) {
    // Don't cache for a volatile field, and return a negative offset as marker
    // of volatile.
    return -resolved_field->GetOffset().Uint32Value();
  }
  // For iput-object, try to resolve the field type even if we were not requested to.
  // Only if the field type is successfully resolved can we update the cache. If we
  // fail to resolve the type, we clear the exception to keep interpreter
  // semantics of not throwing when null is stored.
  if (opcode == Instruction::IPUT_OBJECT &&
      resolve_field_type == 0 &&
      resolved_field->ResolveType() == nullptr) {
    DCHECK(self->IsExceptionPending());
    self->ClearException();
  } else {
    UpdateCache(self, dex_pc_ptr, resolved_field->GetOffset().Uint32Value());
  }
  return resolved_field->GetOffset().Uint32Value();
}

extern "C" void* artNterpAsmInstructionStart[] = { nullptr };
extern "C" void* artNterpAsmInstructionEnd[] = { nullptr };

}  // namespace interpreter
}  // namespace art

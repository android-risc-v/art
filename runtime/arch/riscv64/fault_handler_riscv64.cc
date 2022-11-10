/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "fault_handler.h"

#include <sys/ucontext.h>

#include "arch/instruction_set.h"
#include "art_method.h"
#include "base/enums.h"
#include "base/hex_dump.h"
#include "base/logging.h"
#include "base/macros.h"
#include "registers_riscv64.h"
#include "runtime_globals.h"
#include "thread-current-inl.h"

extern "C" void art_quick_throw_stack_overflow();
extern "C" void art_quick_throw_null_pointer_exception_from_signal();
extern "C" void art_quick_implicit_suspend();

//
// RISCV64 specific fault handler functions.
//

namespace art {

uintptr_t FaultManager::GetFaultPc(siginfo_t*, void*) {
  LOG(FATAL) << "FaultManager::GetFaultPc";
  return 0;
}

uintptr_t FaultManager::GetFaultSp(void*) {
  LOG(FATAL) << "FaultManager::GetFaultSp";
  return 0;
}

/*
void FaultManager::GetMethodAndReturnPcAndSp(siginfo_t* siginfo,
                                             void* context,
                                             ArtMethod** out_method,
                                             uintptr_t* out_return_pc,
                                             uintptr_t* out_sp,
                                             bool* out_is_stack_overflow) {
  struct ucontext_t *uc = reinterpret_cast<struct ucontext_t *>(context);
  struct mcontext_t *sc = reinterpret_cast<struct mcontext_t*>(&uc->uc_mcontext);

  // SEGV_MTEAERR (Async MTE fault) is delivered at an arbitrary point after the actual fault.
  // Register contents, including PC and SP, are unrelated to the fault and can only confuse ART
  // signal handlers.
  if (siginfo->si_signo == SIGSEGV && siginfo->si_code == SEGV_MTEAERR) {
    return;
  }

  *out_sp = static_cast<uintptr_t>(sc->__gregs[REG_SP]);
  VLOG(signals) << "sp: " << *out_sp;
  if (*out_sp == 0) {
    return;
  }

  // In the case of a stack overflow, the stack is not valid and we can't
  // get the method from the top of the stack.  However it's in x0.
  uintptr_t* fault_addr = reinterpret_cast<uintptr_t*>(sc->__gregs[REG_PC]); // fault address?
  uintptr_t* overflow_addr = reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(*out_sp)
      - GetStackOverflowReservedBytes(InstructionSet::kRiscv64));
  if (overflow_addr == fault_addr) {
    *out_method = reinterpret_cast<ArtMethod*>(sc->__gregs[REG_PC]);
    *out_is_stack_overflow = true;
  } else {
    // The method is at the top of the stack.
    *out_method = *reinterpret_cast<ArtMethod**>(*out_sp);
    *out_is_stack_overflow = false;
  }

  // Work out the return PC.  This will be the address of the instruction
  // following the faulting ldr/str instruction.
  VLOG(signals) << "pc: " << std::hex
      << static_cast<void*>(reinterpret_cast<uint8_t*>(sc->__gregs[REG_PC]));

  *out_return_pc = sc->__gregs[REG_PC] + 4;
}
*/

bool NullPointerHandler::Action(int, siginfo_t*, void*) {
  LOG(FATAL) << "NullPointerHandler::Action";
  return false;
}

bool SuspensionHandler::Action(int, siginfo_t*, void*) {
  LOG(FATAL) << "SuspensionHandler::Action";
  return false;
}

bool StackOverflowHandler::Action(int, siginfo_t*, void*) {
  LOG(FATAL) << "StackOverflowHandler::Action";
  return false;
}

}       // namespace art

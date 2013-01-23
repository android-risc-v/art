/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "oat_writer.h"

#include <zlib.h>

#include "base/stl_util.h"
#include "base/unix_file/fd_file.h"
#include "class_linker.h"
#include "class_loader.h"
#include "os.h"
#include "output_stream.h"
#include "safe_map.h"
#include "scoped_thread_state_change.h"
#include "gc/space.h"
#include "verifier/method_verifier.h"

namespace art {

bool OatWriter::Create(OutputStream& output_stream,
                       const std::vector<const DexFile*>& dex_files,
                       uint32_t image_file_location_oat_checksum,
                       uint32_t image_file_location_oat_begin,
                       const std::string& image_file_location,
                       const Compiler& compiler) {
  OatWriter oat_writer(dex_files,
                       image_file_location_oat_checksum,
                       image_file_location_oat_begin,
                       image_file_location,
                       compiler);
  return oat_writer.Write(output_stream);
}

OatWriter::OatWriter(const std::vector<const DexFile*>& dex_files,
                     uint32_t image_file_location_oat_checksum,
                     uint32_t image_file_location_oat_begin,
                     const std::string& image_file_location,
                     const Compiler& compiler) {
  compiler_ = &compiler;
  image_file_location_oat_checksum_ = image_file_location_oat_checksum;
  image_file_location_oat_begin_ = image_file_location_oat_begin;
  image_file_location_ = image_file_location;
  dex_files_ = &dex_files;
  oat_header_ = NULL;
  executable_offset_padding_length_ = 0;

  size_t offset = InitOatHeader();
  offset = InitOatDexFiles(offset);
  offset = InitDexFiles(offset);
  offset = InitOatClasses(offset);
  offset = InitOatCode(offset);
  offset = InitOatCodeDexFiles(offset);

  CHECK_EQ(dex_files_->size(), oat_dex_files_.size());
}

OatWriter::~OatWriter() {
  delete oat_header_;
  STLDeleteElements(&oat_dex_files_);
  STLDeleteElements(&oat_classes_);
}

size_t OatWriter::InitOatHeader() {
  // create the OatHeader
  oat_header_ = new OatHeader(compiler_->GetInstructionSet(),
                              dex_files_,
                              image_file_location_oat_checksum_,
                              image_file_location_oat_begin_,
                              image_file_location_);
  size_t offset = sizeof(*oat_header_);
  offset += image_file_location_.size();
  return offset;
}

size_t OatWriter::InitOatDexFiles(size_t offset) {
  // create the OatDexFiles
  for (size_t i = 0; i != dex_files_->size(); ++i) {
    const DexFile* dex_file = (*dex_files_)[i];
    CHECK(dex_file != NULL);
    OatDexFile* oat_dex_file = new OatDexFile(*dex_file);
    oat_dex_files_.push_back(oat_dex_file);
    offset += oat_dex_file->SizeOf();
  }
  return offset;
}

size_t OatWriter::InitDexFiles(size_t offset) {
  // calculate the offsets within OatDexFiles to the DexFiles
  for (size_t i = 0; i != dex_files_->size(); ++i) {
    // dex files are required to be 4 byte aligned
    offset = RoundUp(offset, 4);

    // set offset in OatDexFile to DexFile
    oat_dex_files_[i]->dex_file_offset_ = offset;

    const DexFile* dex_file = (*dex_files_)[i];
    offset += dex_file->GetHeader().file_size_;
  }
  return offset;
}

size_t OatWriter::InitOatClasses(size_t offset) {
  // create the OatClasses
  // calculate the offsets within OatDexFiles to OatClasses
  for (size_t i = 0; i != dex_files_->size(); ++i) {
    const DexFile* dex_file = (*dex_files_)[i];
    for (size_t class_def_index = 0;
         class_def_index < dex_file->NumClassDefs();
         class_def_index++) {
      oat_dex_files_[i]->methods_offsets_[class_def_index] = offset;
      const DexFile::ClassDef& class_def = dex_file->GetClassDef(class_def_index);
      const byte* class_data = dex_file->GetClassData(class_def);
      uint32_t num_methods = 0;
      if (class_data != NULL) {  // ie not an empty class, such as a marker interface
        ClassDataItemIterator it(*dex_file, class_data);
        size_t num_direct_methods = it.NumDirectMethods();
        size_t num_virtual_methods = it.NumVirtualMethods();
        num_methods = num_direct_methods + num_virtual_methods;
      }

      Compiler::ClassReference class_ref = Compiler::ClassReference(dex_file, class_def_index);
      CompiledClass* compiled_class = compiler_->GetCompiledClass(class_ref);
      Class::Status status;
      if (compiled_class != NULL) {
        status = compiled_class->GetStatus();
      } else if (verifier::MethodVerifier::IsClassRejected(class_ref)) {
        status = Class::kStatusError;
      } else {
        status = Class::kStatusNotReady;
      }

      OatClass* oat_class = new OatClass(status, num_methods);
      oat_classes_.push_back(oat_class);
      offset += oat_class->SizeOf();
    }
    oat_dex_files_[i]->UpdateChecksum(*oat_header_);
  }
  return offset;
}

size_t OatWriter::InitOatCode(size_t offset) {
  // calculate the offsets within OatHeader to executable code
  size_t old_offset = offset;
  // required to be on a new page boundary
  offset = RoundUp(offset, kPageSize);
  oat_header_->SetExecutableOffset(offset);
  executable_offset_padding_length_ = offset - old_offset;
  return offset;
}

size_t OatWriter::InitOatCodeDexFiles(size_t offset) {
  size_t oat_class_index = 0;
  for (size_t i = 0; i != dex_files_->size(); ++i) {
    const DexFile* dex_file = (*dex_files_)[i];
    CHECK(dex_file != NULL);
    offset = InitOatCodeDexFile(offset, oat_class_index, *dex_file);
  }
  return offset;
}

size_t OatWriter::InitOatCodeDexFile(size_t offset,
                                     size_t& oat_class_index,
                                     const DexFile& dex_file) {
  for (size_t class_def_index = 0;
       class_def_index < dex_file.NumClassDefs();
       class_def_index++, oat_class_index++) {
    const DexFile::ClassDef& class_def = dex_file.GetClassDef(class_def_index);
    offset = InitOatCodeClassDef(offset, oat_class_index, class_def_index, dex_file, class_def);
    oat_classes_[oat_class_index]->UpdateChecksum(*oat_header_);
  }
  return offset;
}

size_t OatWriter::InitOatCodeClassDef(size_t offset,
                                      size_t oat_class_index, size_t class_def_index,
                                      const DexFile& dex_file,
                                      const DexFile::ClassDef& class_def) {
  const byte* class_data = dex_file.GetClassData(class_def);
  if (class_data == NULL) {
    // empty class, such as a marker interface
    return offset;
  }
  ClassDataItemIterator it(dex_file, class_data);
  CHECK_EQ(oat_classes_[oat_class_index]->method_offsets_.size(),
           it.NumDirectMethods() + it.NumVirtualMethods());
  // Skip fields
  while (it.HasNextStaticField()) {
    it.Next();
  }
  while (it.HasNextInstanceField()) {
    it.Next();
  }
  // Process methods
  size_t class_def_method_index = 0;
  while (it.HasNextDirectMethod()) {
    bool is_native = (it.GetMemberAccessFlags() & kAccNative) != 0;
    offset = InitOatCodeMethod(offset, oat_class_index, class_def_index, class_def_method_index,
                               is_native, it.GetMethodInvokeType(class_def), it.GetMemberIndex(),
                               &dex_file);
    class_def_method_index++;
    it.Next();
  }
  while (it.HasNextVirtualMethod()) {
    bool is_native = (it.GetMemberAccessFlags() & kAccNative) != 0;
    offset = InitOatCodeMethod(offset, oat_class_index, class_def_index, class_def_method_index,
                               is_native, it.GetMethodInvokeType(class_def), it.GetMemberIndex(),
                               &dex_file);
    class_def_method_index++;
    it.Next();
  }
  DCHECK(!it.HasNext());
  return offset;
}

size_t OatWriter::InitOatCodeMethod(size_t offset, size_t oat_class_index,
                                    size_t __attribute__((unused)) class_def_index,
                                    size_t class_def_method_index,
                                    bool __attribute__((unused)) is_native,
                                    InvokeType type,
                                    uint32_t method_idx, const DexFile* dex_file) {
  // derived from CompiledMethod if available
  uint32_t code_offset = 0;
  uint32_t frame_size_in_bytes = kStackAlignment;
  uint32_t core_spill_mask = 0;
  uint32_t fp_spill_mask = 0;
  uint32_t mapping_table_offset = 0;
  uint32_t vmap_table_offset = 0;
  uint32_t gc_map_offset = 0;
  // derived from CompiledInvokeStub if available
  uint32_t invoke_stub_offset = 0;
#if defined(ART_USE_LLVM_COMPILER)
  uint32_t proxy_stub_offset = 0;
#endif

  CompiledMethod* compiled_method =
      compiler_->GetCompiledMethod(Compiler::MethodReference(dex_file, method_idx));
  if (compiled_method != NULL) {
    offset = compiled_method->AlignCode(offset);
    DCHECK_ALIGNED(offset, kArmAlignment);
    const std::vector<uint8_t>& code = compiled_method->GetCode();
    uint32_t code_size = code.size() * sizeof(code[0]);
    CHECK_NE(code_size, 0U);
    uint32_t thumb_offset = compiled_method->CodeDelta();
    code_offset = offset + sizeof(code_size) + thumb_offset;

    // Deduplicate code arrays
    SafeMap<const std::vector<uint8_t>*, uint32_t>::iterator code_iter = code_offsets_.find(&code);
    if (code_iter != code_offsets_.end()) {
      code_offset = code_iter->second;
    } else {
      code_offsets_.Put(&code, code_offset);
      offset += sizeof(code_size);  // code size is prepended before code
      offset += code_size;
      oat_header_->UpdateChecksum(&code[0], code_size);
    }
    frame_size_in_bytes = compiled_method->GetFrameSizeInBytes();
    core_spill_mask = compiled_method->GetCoreSpillMask();
    fp_spill_mask = compiled_method->GetFpSpillMask();

    const std::vector<uint32_t>& mapping_table = compiled_method->GetMappingTable();
    size_t mapping_table_size = mapping_table.size() * sizeof(mapping_table[0]);
    mapping_table_offset = (mapping_table_size == 0) ? 0 : offset;

    // Deduplicate mapping tables
    SafeMap<const std::vector<uint32_t>*, uint32_t>::iterator mapping_iter = mapping_table_offsets_.find(&mapping_table);
    if (mapping_iter != mapping_table_offsets_.end()) {
      mapping_table_offset = mapping_iter->second;
    } else {
      mapping_table_offsets_.Put(&mapping_table, mapping_table_offset);
      offset += mapping_table_size;
      oat_header_->UpdateChecksum(&mapping_table[0], mapping_table_size);
    }

    const std::vector<uint16_t>& vmap_table = compiled_method->GetVmapTable();
    size_t vmap_table_size = vmap_table.size() * sizeof(vmap_table[0]);
    vmap_table_offset = (vmap_table_size == 0) ? 0 : offset;

    // Deduplicate vmap tables
    SafeMap<const std::vector<uint16_t>*, uint32_t>::iterator vmap_iter = vmap_table_offsets_.find(&vmap_table);
    if (vmap_iter != vmap_table_offsets_.end()) {
      vmap_table_offset = vmap_iter->second;
    } else {
      vmap_table_offsets_.Put(&vmap_table, vmap_table_offset);
      offset += vmap_table_size;
      oat_header_->UpdateChecksum(&vmap_table[0], vmap_table_size);
    }

    const std::vector<uint8_t>& gc_map = compiled_method->GetNativeGcMap();
    size_t gc_map_size = gc_map.size() * sizeof(gc_map[0]);
    gc_map_offset = (gc_map_size == 0) ? 0 : offset;

#if !defined(NDEBUG)
    // We expect GC maps except when the class hasn't been verified or the method is native
    Compiler::ClassReference class_ref = Compiler::ClassReference(dex_file, class_def_index);
    CompiledClass* compiled_class = compiler_->GetCompiledClass(class_ref);
    Class::Status status;
    if (compiled_class != NULL) {
      status = compiled_class->GetStatus();
    } else if (verifier::MethodVerifier::IsClassRejected(class_ref)) {
      status = Class::kStatusError;
    } else {
      status = Class::kStatusNotReady;
    }
    CHECK(gc_map_size != 0 || is_native || status < Class::kStatusVerified)
        << &gc_map << " " << gc_map_size << " " << (is_native ? "true" : "false") << " " << (status < Class::kStatusVerified) << " " << status << " " << PrettyMethod(method_idx, *dex_file);
#endif

    // Deduplicate GC maps
    SafeMap<const std::vector<uint8_t>*, uint32_t>::iterator gc_map_iter = gc_map_offsets_.find(&gc_map);
    if (gc_map_iter != gc_map_offsets_.end()) {
      gc_map_offset = gc_map_iter->second;
    } else {
      gc_map_offsets_.Put(&gc_map, gc_map_offset);
      offset += gc_map_size;
      oat_header_->UpdateChecksum(&gc_map[0], gc_map_size);
    }
  }

  const char* shorty = dex_file->GetMethodShorty(dex_file->GetMethodId(method_idx));
  const CompiledInvokeStub* compiled_invoke_stub = compiler_->FindInvokeStub(type == kStatic,
                                                                             shorty);
  if (compiled_invoke_stub != NULL) {
    offset = CompiledMethod::AlignCode(offset, compiler_->GetInstructionSet());
    DCHECK_ALIGNED(offset, kArmAlignment);
    const std::vector<uint8_t>& invoke_stub = compiled_invoke_stub->GetCode();
    uint32_t invoke_stub_size = invoke_stub.size() * sizeof(invoke_stub[0]);
    CHECK_NE(invoke_stub_size, 0U);
    uint32_t thumb_offset = compiled_invoke_stub->CodeDelta();
    invoke_stub_offset = offset + sizeof(invoke_stub_size) + thumb_offset;

    // Deduplicate invoke stubs
    SafeMap<const std::vector<uint8_t>*, uint32_t>::iterator stub_iter = code_offsets_.find(&invoke_stub);
    if (stub_iter != code_offsets_.end()) {
      invoke_stub_offset = stub_iter->second;
    } else {
      code_offsets_.Put(&invoke_stub, invoke_stub_offset);
      offset += sizeof(invoke_stub_size);  // invoke stub size is prepended before code
      offset += invoke_stub_size;
      oat_header_->UpdateChecksum(&invoke_stub[0], invoke_stub_size);
    }
  }

#if defined(ART_USE_LLVM_COMPILER)
  if (type != kStatic) {
    const CompiledInvokeStub* compiled_proxy_stub = compiler_->FindProxyStub(shorty);
    if (compiled_proxy_stub != NULL) {
      offset = CompiledMethod::AlignCode(offset, compiler_->GetInstructionSet());
      DCHECK_ALIGNED(offset, kArmAlignment);
      const std::vector<uint8_t>& proxy_stub = compiled_proxy_stub->GetCode();
      uint32_t proxy_stub_size = proxy_stub.size() * sizeof(proxy_stub[0]);
      CHECK_NE(proxy_stub_size, 0U);
      uint32_t thumb_offset = compiled_proxy_stub->CodeDelta();
      proxy_stub_offset = offset + sizeof(proxy_stub_size) + thumb_offset;

      // Deduplicate proxy stubs
      SafeMap<const std::vector<uint8_t>*, uint32_t>::iterator stub_iter = code_offsets_.find(&proxy_stub);
      if (stub_iter != code_offsets_.end()) {
        proxy_stub_offset = stub_iter->second;
      } else {
        code_offsets_.Put(&proxy_stub, proxy_stub_offset);
        offset += sizeof(proxy_stub_size);  // proxy stub size is prepended before code
        offset += proxy_stub_size;
        oat_header_->UpdateChecksum(&proxy_stub[0], proxy_stub_size);
      }
    }
  }
#endif

  oat_classes_[oat_class_index]->method_offsets_[class_def_method_index]
      = OatMethodOffsets(code_offset,
                         frame_size_in_bytes,
                         core_spill_mask,
                         fp_spill_mask,
                         mapping_table_offset,
                         vmap_table_offset,
                         gc_map_offset,
                         invoke_stub_offset
#if defined(ART_USE_LLVM_COMPILER)
                       , proxy_stub_offset
#endif
                         );

  if (compiler_->IsImage()) {
    ClassLinker* linker = Runtime::Current()->GetClassLinker();
    DexCache* dex_cache = linker->FindDexCache(*dex_file);
    // Unchecked as we hold mutator_lock_ on entry.
    ScopedObjectAccessUnchecked soa(Thread::Current());
    AbstractMethod* method = linker->ResolveMethod(*dex_file, method_idx, dex_cache,
                                                   NULL, NULL, type);
    CHECK(method != NULL);
    method->SetFrameSizeInBytes(frame_size_in_bytes);
    method->SetCoreSpillMask(core_spill_mask);
    method->SetFpSpillMask(fp_spill_mask);
    method->SetOatMappingTableOffset(mapping_table_offset);
    // Don't overwrite static method trampoline
    if (!method->IsStatic() || method->IsConstructor() ||
        method->GetDeclaringClass()->IsInitialized()) {
      method->SetOatCodeOffset(code_offset);
    } else {
      method->SetCode(Runtime::Current()->GetResolutionStubArray(Runtime::kStaticMethod)->GetData());
    }
    method->SetOatVmapTableOffset(vmap_table_offset);
    method->SetOatNativeGcMapOffset(gc_map_offset);
    method->SetOatInvokeStubOffset(invoke_stub_offset);
  }

  return offset;
}

#define DCHECK_CODE_OFFSET() \
  DCHECK_EQ(static_cast<off_t>(code_offset), out.Seek(0, kSeekCurrent))

bool OatWriter::Write(OutputStream& out) {
  if (!out.WriteFully(oat_header_, sizeof(*oat_header_))) {
    PLOG(ERROR) << "Failed to write oat header to " << out.GetLocation();
    return false;
  }

  if (!out.WriteFully(image_file_location_.data(), image_file_location_.size())) {
    PLOG(ERROR) << "Failed to write oat header image file location to " << out.GetLocation();
    return false;
  }

  if (!WriteTables(out)) {
    LOG(ERROR) << "Failed to write oat tables to " << out.GetLocation();
    return false;
  }

  size_t code_offset = WriteCode(out);
  if (code_offset == 0) {
    LOG(ERROR) << "Failed to write oat code to " << out.GetLocation();
    return false;
  }

  code_offset = WriteCodeDexFiles(out, code_offset);
  if (code_offset == 0) {
    LOG(ERROR) << "Failed to write oat code for dex files to " << out.GetLocation();
    return false;
  }

  return true;
}

bool OatWriter::WriteTables(OutputStream& out) {
  for (size_t i = 0; i != oat_dex_files_.size(); ++i) {
    if (!oat_dex_files_[i]->Write(out)) {
      PLOG(ERROR) << "Failed to write oat dex information to " << out.GetLocation();
      return false;
    }
  }
  for (size_t i = 0; i != oat_dex_files_.size(); ++i) {
    uint32_t expected_offset = oat_dex_files_[i]->dex_file_offset_;
    off_t actual_offset = out.Seek(expected_offset, kSeekSet);
    if (static_cast<uint32_t>(actual_offset) != expected_offset) {
      const DexFile* dex_file = (*dex_files_)[i];
      PLOG(ERROR) << "Failed to seek to dex file section. Actual: " << actual_offset
                  << " Expected: " << expected_offset << " File: " << dex_file->GetLocation();
      return false;
    }
    const DexFile* dex_file = (*dex_files_)[i];
    if (!out.WriteFully(&dex_file->GetHeader(), dex_file->GetHeader().file_size_)) {
      PLOG(ERROR) << "Failed to write dex file " << dex_file->GetLocation() << " to " << out.GetLocation();
      return false;
    }
  }
  for (size_t i = 0; i != oat_classes_.size(); ++i) {
    if (!oat_classes_[i]->Write(out)) {
      PLOG(ERROR) << "Failed to write oat methods information to " << out.GetLocation();
      return false;
    }
  }
  return true;
}

size_t OatWriter::WriteCode(OutputStream& out) {
  uint32_t code_offset = oat_header_->GetExecutableOffset();
  off_t new_offset = out.Seek(executable_offset_padding_length_, kSeekCurrent);
  if (static_cast<uint32_t>(new_offset) != code_offset) {
    PLOG(ERROR) << "Failed to seek to oat code section. Actual: " << new_offset
                << " Expected: " << code_offset << " File: " << out.GetLocation();
    return 0;
  }
  DCHECK_CODE_OFFSET();
  return code_offset;
}

size_t OatWriter::WriteCodeDexFiles(OutputStream& out, size_t code_offset) {
  size_t oat_class_index = 0;
  for (size_t i = 0; i != oat_dex_files_.size(); ++i) {
    const DexFile* dex_file = (*dex_files_)[i];
    CHECK(dex_file != NULL);
    code_offset = WriteCodeDexFile(out, code_offset, oat_class_index, *dex_file);
    if (code_offset == 0) {
      return 0;
    }
  }
  return code_offset;
}

size_t OatWriter::WriteCodeDexFile(OutputStream& out, size_t code_offset, size_t& oat_class_index,
                                   const DexFile& dex_file) {
  for (size_t class_def_index = 0; class_def_index < dex_file.NumClassDefs();
      class_def_index++, oat_class_index++) {
    const DexFile::ClassDef& class_def = dex_file.GetClassDef(class_def_index);
    code_offset = WriteCodeClassDef(out, code_offset, oat_class_index, dex_file, class_def);
    if (code_offset == 0) {
      return 0;
    }
  }
  return code_offset;
}

void OatWriter::ReportWriteFailure(const char* what, uint32_t method_idx,
                                   const DexFile& dex_file, OutputStream& out) const {
  PLOG(ERROR) << "Failed to write " << what << " for " << PrettyMethod(method_idx, dex_file)
      << " to " << out.GetLocation();
}

size_t OatWriter::WriteCodeClassDef(OutputStream& out,
                                    size_t code_offset, size_t oat_class_index,
                                    const DexFile& dex_file,
                                    const DexFile::ClassDef& class_def) {
  const byte* class_data = dex_file.GetClassData(class_def);
  if (class_data == NULL) {
    // ie. an empty class such as a marker interface
    return code_offset;
  }
  ClassDataItemIterator it(dex_file, class_data);
  // Skip fields
  while (it.HasNextStaticField()) {
    it.Next();
  }
  while (it.HasNextInstanceField()) {
    it.Next();
  }
  // Process methods
  size_t class_def_method_index = 0;
  while (it.HasNextDirectMethod()) {
    bool is_static = (it.GetMemberAccessFlags() & kAccStatic) != 0;
    code_offset = WriteCodeMethod(out, code_offset, oat_class_index, class_def_method_index,
                                  is_static, it.GetMemberIndex(), dex_file);
    if (code_offset == 0) {
      return 0;
    }
    class_def_method_index++;
    it.Next();
  }
  while (it.HasNextVirtualMethod()) {
    code_offset = WriteCodeMethod(out, code_offset, oat_class_index, class_def_method_index,
                                  false, it.GetMemberIndex(), dex_file);
    if (code_offset == 0) {
      return 0;
    }
    class_def_method_index++;
    it.Next();
  }
  return code_offset;
}

size_t OatWriter::WriteCodeMethod(OutputStream& out, size_t code_offset, size_t oat_class_index,
                                  size_t class_def_method_index, bool is_static,
                                  uint32_t method_idx, const DexFile& dex_file) {
  const CompiledMethod* compiled_method =
      compiler_->GetCompiledMethod(Compiler::MethodReference(&dex_file, method_idx));

  OatMethodOffsets method_offsets =
      oat_classes_[oat_class_index]->method_offsets_[class_def_method_index];


  if (compiled_method != NULL) {  // ie. not an abstract method
    uint32_t aligned_code_offset = compiled_method->AlignCode(code_offset);
    uint32_t aligned_code_delta = aligned_code_offset - code_offset;
    if (aligned_code_delta != 0) {
      off_t new_offset = out.Seek(aligned_code_delta, kSeekCurrent);
      if (static_cast<uint32_t>(new_offset) != aligned_code_offset) {
        PLOG(ERROR) << "Failed to seek to align oat code. Actual: " << new_offset
                    << " Expected: " << aligned_code_offset << " File: " << out.GetLocation();
        return 0;
      }
      code_offset += aligned_code_delta;
      DCHECK_CODE_OFFSET();
    }
    DCHECK_ALIGNED(code_offset, kArmAlignment);
    const std::vector<uint8_t>& code = compiled_method->GetCode();
    uint32_t code_size = code.size() * sizeof(code[0]);
    CHECK_NE(code_size, 0U);

    // Deduplicate code arrays
    size_t offset = code_offset + sizeof(code_size) + compiled_method->CodeDelta();
    SafeMap<const std::vector<uint8_t>*, uint32_t>::iterator code_iter = code_offsets_.find(&code);
    if (code_iter != code_offsets_.end() && offset != method_offsets.code_offset_) {
      DCHECK(code_iter->second == method_offsets.code_offset_) << PrettyMethod(method_idx, dex_file);
    } else {
      DCHECK(offset == method_offsets.code_offset_) << PrettyMethod(method_idx, dex_file);
      if (!out.WriteFully(&code_size, sizeof(code_size))) {
        ReportWriteFailure("method code size", method_idx, dex_file, out);
        return 0;
      }
      code_offset += sizeof(code_size);
      DCHECK_CODE_OFFSET();
      if (!out.WriteFully(&code[0], code_size)) {
        ReportWriteFailure("method code", method_idx, dex_file, out);
        return 0;
      }
      code_offset += code_size;
    }
    DCHECK_CODE_OFFSET();

    const std::vector<uint32_t>& mapping_table = compiled_method->GetMappingTable();
    size_t mapping_table_size = mapping_table.size() * sizeof(mapping_table[0]);

    // Deduplicate mapping tables
    SafeMap<const std::vector<uint32_t>*, uint32_t>::iterator mapping_iter =
        mapping_table_offsets_.find(&mapping_table);
    if (mapping_iter != mapping_table_offsets_.end() &&
        code_offset != method_offsets.mapping_table_offset_) {
      DCHECK((mapping_table_size == 0 && method_offsets.mapping_table_offset_ == 0)
          || mapping_iter->second == method_offsets.mapping_table_offset_)
          << PrettyMethod(method_idx, dex_file);
    } else {
      DCHECK((mapping_table_size == 0 && method_offsets.mapping_table_offset_ == 0)
          || code_offset == method_offsets.mapping_table_offset_)
          << PrettyMethod(method_idx, dex_file);
      if (!out.WriteFully(&mapping_table[0], mapping_table_size)) {
        ReportWriteFailure("mapping table", method_idx, dex_file, out);
        return 0;
      }
      code_offset += mapping_table_size;
    }
    DCHECK_CODE_OFFSET();

    const std::vector<uint16_t>& vmap_table = compiled_method->GetVmapTable();
    size_t vmap_table_size = vmap_table.size() * sizeof(vmap_table[0]);

    // Deduplicate vmap tables
    SafeMap<const std::vector<uint16_t>*, uint32_t>::iterator vmap_iter =
        vmap_table_offsets_.find(&vmap_table);
    if (vmap_iter != vmap_table_offsets_.end() &&
        code_offset != method_offsets.vmap_table_offset_) {
      DCHECK((vmap_table_size == 0 && method_offsets.vmap_table_offset_ == 0)
          || vmap_iter->second == method_offsets.vmap_table_offset_)
          << PrettyMethod(method_idx, dex_file);
    } else {
      DCHECK((vmap_table_size == 0 && method_offsets.vmap_table_offset_ == 0)
          || code_offset == method_offsets.vmap_table_offset_)
          << PrettyMethod(method_idx, dex_file);
      if (!out.WriteFully(&vmap_table[0], vmap_table_size)) {
        ReportWriteFailure("vmap table", method_idx, dex_file, out);
        return 0;
      }
      code_offset += vmap_table_size;
    }
    DCHECK_CODE_OFFSET();

    const std::vector<uint8_t>& gc_map = compiled_method->GetNativeGcMap();
    size_t gc_map_size = gc_map.size() * sizeof(gc_map[0]);

    // Deduplicate GC maps
    SafeMap<const std::vector<uint8_t>*, uint32_t>::iterator gc_map_iter =
        gc_map_offsets_.find(&gc_map);
    if (gc_map_iter != gc_map_offsets_.end() &&
        code_offset != method_offsets.gc_map_offset_) {
      DCHECK((gc_map_size == 0 && method_offsets.gc_map_offset_ == 0)
          || gc_map_iter->second == method_offsets.gc_map_offset_)
          << PrettyMethod(method_idx, dex_file);
    } else {
      DCHECK((gc_map_size == 0 && method_offsets.gc_map_offset_ == 0)
          || code_offset == method_offsets.gc_map_offset_)
          << PrettyMethod(method_idx, dex_file);
      if (!out.WriteFully(&gc_map[0], gc_map_size)) {
        ReportWriteFailure("GC map", method_idx, dex_file, out);
        return 0;
      }
      code_offset += gc_map_size;
    }
    DCHECK_CODE_OFFSET();
  }
  const char* shorty = dex_file.GetMethodShorty(dex_file.GetMethodId(method_idx));
  const CompiledInvokeStub* compiled_invoke_stub = compiler_->FindInvokeStub(is_static, shorty);
  if (compiled_invoke_stub != NULL) {
    uint32_t aligned_code_offset = CompiledMethod::AlignCode(code_offset,
                                                             compiler_->GetInstructionSet());
    uint32_t aligned_code_delta = aligned_code_offset - code_offset;
    if (aligned_code_delta != 0) {
      off_t new_offset = out.Seek(aligned_code_delta, kSeekCurrent);
      if (static_cast<uint32_t>(new_offset) != aligned_code_offset) {
        PLOG(ERROR) << "Failed to seek to align invoke stub code. Actual: " << new_offset
                    << " Expected: " << aligned_code_offset;
        return 0;
      }
      code_offset += aligned_code_delta;
      DCHECK_CODE_OFFSET();
    }
    DCHECK_ALIGNED(code_offset, kArmAlignment);
    const std::vector<uint8_t>& invoke_stub = compiled_invoke_stub->GetCode();
    uint32_t invoke_stub_size = invoke_stub.size() * sizeof(invoke_stub[0]);
    CHECK_NE(invoke_stub_size, 0U);

    // Deduplicate invoke stubs
    size_t offset = code_offset + sizeof(invoke_stub_size) + compiled_invoke_stub->CodeDelta();
    SafeMap<const std::vector<uint8_t>*, uint32_t>::iterator stub_iter =
        code_offsets_.find(&invoke_stub);
    if (stub_iter != code_offsets_.end() && offset != method_offsets.invoke_stub_offset_) {
      DCHECK(stub_iter->second == method_offsets.invoke_stub_offset_) << PrettyMethod(method_idx, dex_file);
    } else {
      DCHECK(offset == method_offsets.invoke_stub_offset_) << PrettyMethod(method_idx, dex_file);
      if (!out.WriteFully(&invoke_stub_size, sizeof(invoke_stub_size))) {
        ReportWriteFailure("invoke stub code size", method_idx, dex_file, out);
        return 0;
      }
      code_offset += sizeof(invoke_stub_size);
      DCHECK_CODE_OFFSET();
      if (!out.WriteFully(&invoke_stub[0], invoke_stub_size)) {
        ReportWriteFailure("invoke stub code", method_idx, dex_file, out);
        return 0;
      }
      code_offset += invoke_stub_size;
      DCHECK_CODE_OFFSET();
    }
  }

#if defined(ART_USE_LLVM_COMPILER)
  if (!is_static) {
    const CompiledInvokeStub* compiled_proxy_stub = compiler_->FindProxyStub(shorty);
    if (compiled_proxy_stub != NULL) {
      uint32_t aligned_code_offset = CompiledMethod::AlignCode(code_offset,
                                                               compiler_->GetInstructionSet());
      uint32_t aligned_code_delta = aligned_code_offset - code_offset;
      CHECK(aligned_code_delta < 48u);
      if (aligned_code_delta != 0) {
        off_t new_offset = out.Seek(aligned_code_delta, kSeekCurrent);
        if (static_cast<uint32_t>(new_offset) != aligned_code_offset) {
          PLOG(ERROR) << "Failed to seek to align proxy stub code. Actual: " << new_offset
                      << " Expected: " << aligned_code_offset;
          return 0;
        }
        code_offset += aligned_code_delta;
        DCHECK_CODE_OFFSET();
      }
      DCHECK_ALIGNED(code_offset, kArmAlignment);
      const std::vector<uint8_t>& proxy_stub = compiled_proxy_stub->GetCode();
      uint32_t proxy_stub_size = proxy_stub.size() * sizeof(proxy_stub[0]);
      CHECK_NE(proxy_stub_size, 0U);

      // Deduplicate proxy stubs
      size_t offset = code_offset + sizeof(proxy_stub_size) + compiled_proxy_stub->CodeDelta();
      SafeMap<const std::vector<uint8_t>*, uint32_t>::iterator stub_iter =
          code_offsets_.find(&proxy_stub);
      if (stub_iter != code_offsets_.end() && offset != method_offsets.proxy_stub_offset_) {
        DCHECK(stub_iter->second == method_offsets.proxy_stub_offset_) << PrettyMethod(method_idx, dex_file);
      } else {
        DCHECK(offset == method_offsets.proxy_stub_offset_) << PrettyMethod(method_idx, dex_file);
        if (!out.WriteFully(&proxy_stub_size, sizeof(proxy_stub_size))) {
          ReportWriteFailure("proxy stub code size", method_idx, dex_file, out);
          return 0;
        }
        code_offset += sizeof(proxy_stub_size);
        DCHECK_CODE_OFFSET();
        if (!out.WriteFully(&proxy_stub[0], proxy_stub_size)) {
          ReportWriteFailure("proxy stub code", method_idx, dex_file, out);
          return 0;
        }
        code_offset += proxy_stub_size;
        DCHECK_CODE_OFFSET();
      }
      DCHECK_CODE_OFFSET();
    }
  }
#endif

  return code_offset;
}

OatWriter::OatDexFile::OatDexFile(const DexFile& dex_file) {
  const std::string& location(dex_file.GetLocation());
  dex_file_location_size_ = location.size();
  dex_file_location_data_ = reinterpret_cast<const uint8_t*>(location.data());
  dex_file_location_checksum_ = dex_file.GetLocationChecksum();
  dex_file_offset_ = 0;
  methods_offsets_.resize(dex_file.NumClassDefs());
}

size_t OatWriter::OatDexFile::SizeOf() const {
  return sizeof(dex_file_location_size_)
          + dex_file_location_size_
          + sizeof(dex_file_location_checksum_)
          + sizeof(dex_file_offset_)
          + (sizeof(methods_offsets_[0]) * methods_offsets_.size());
}

void OatWriter::OatDexFile::UpdateChecksum(OatHeader& oat_header) const {
  oat_header.UpdateChecksum(&dex_file_location_size_, sizeof(dex_file_location_size_));
  oat_header.UpdateChecksum(dex_file_location_data_, dex_file_location_size_);
  oat_header.UpdateChecksum(&dex_file_location_checksum_, sizeof(dex_file_location_checksum_));
  oat_header.UpdateChecksum(&dex_file_offset_, sizeof(dex_file_offset_));
  oat_header.UpdateChecksum(&methods_offsets_[0],
                            sizeof(methods_offsets_[0]) * methods_offsets_.size());
}

bool OatWriter::OatDexFile::Write(OutputStream& out) const {
  if (!out.WriteFully(&dex_file_location_size_, sizeof(dex_file_location_size_))) {
    PLOG(ERROR) << "Failed to write dex file location length to " << out.GetLocation();
    return false;
  }
  if (!out.WriteFully(dex_file_location_data_, dex_file_location_size_)) {
    PLOG(ERROR) << "Failed to write dex file location data to " << out.GetLocation();
    return false;
  }
  if (!out.WriteFully(&dex_file_location_checksum_, sizeof(dex_file_location_checksum_))) {
    PLOG(ERROR) << "Failed to write dex file location checksum to " << out.GetLocation();
    return false;
  }
  if (!out.WriteFully(&dex_file_offset_, sizeof(dex_file_offset_))) {
    PLOG(ERROR) << "Failed to write dex file offset to " << out.GetLocation();
    return false;
  }
  if (!out.WriteFully(&methods_offsets_[0],
                      sizeof(methods_offsets_[0]) * methods_offsets_.size())) {
    PLOG(ERROR) << "Failed to write methods offsets to " << out.GetLocation();
    return false;
  }
  return true;
}

OatWriter::OatClass::OatClass(Class::Status status, uint32_t methods_count) {
  status_ = status;
  method_offsets_.resize(methods_count);
}

size_t OatWriter::OatClass::SizeOf() const {
  return sizeof(status_)
          + (sizeof(method_offsets_[0]) * method_offsets_.size());
}

void OatWriter::OatClass::UpdateChecksum(OatHeader& oat_header) const {
  oat_header.UpdateChecksum(&status_, sizeof(status_));
  oat_header.UpdateChecksum(&method_offsets_[0],
                            sizeof(method_offsets_[0]) * method_offsets_.size());
}

bool OatWriter::OatClass::Write(OutputStream& out) const {
  if (!out.WriteFully(&status_, sizeof(status_))) {
    PLOG(ERROR) << "Failed to write class status to " << out.GetLocation();
    return false;
  }
  if (!out.WriteFully(&method_offsets_[0],
                      sizeof(method_offsets_[0]) * method_offsets_.size())) {
    PLOG(ERROR) << "Failed to write method offsets to " << out.GetLocation();
    return false;
  }
  return true;
}

}  // namespace art

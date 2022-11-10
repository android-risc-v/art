/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "instruction_set_features_riscv64.h"

#if defined(ART_TARGET_ANDROID) && defined(__riscv)
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif

#include <fstream>
#include <sstream>

#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include "base/array_ref.h"
#include "base/stl_util.h"

#include <cpu_features_macros.h>

#ifdef CPU_FEATURES_ARCH_RISCV64
// This header can only be included on riscv64 targets,
// as determined by cpu_features own define.
#include <cpuinfo_riscv64.h>
#endif

namespace art {

using android::base::StringPrintf;

Riscv64FeaturesUniquePtr Riscv64InstructionSetFeatures::FromVariant(
    const std::string&, std::string*) {
  // The CPU variant string is passed to ART through --instruction-set-variant option.
  // During build, such setting is from TARGET_CPU_VARIANT in device BoardConfig.mk.

  return Riscv64FeaturesUniquePtr(new Riscv64InstructionSetFeatures());
}

Riscv64FeaturesUniquePtr Riscv64InstructionSetFeatures::IntersectWithHwcap() const {
  Riscv64FeaturesUniquePtr hwcaps = Riscv64InstructionSetFeatures::FromHwcap();
  return Riscv64FeaturesUniquePtr(new Riscv64InstructionSetFeatures());
}

Riscv64FeaturesUniquePtr Riscv64InstructionSetFeatures::FromBitmap(uint32_t) {
  return Riscv64FeaturesUniquePtr(new Riscv64InstructionSetFeatures());
}

Riscv64FeaturesUniquePtr Riscv64InstructionSetFeatures::FromCppDefines() {
  return Riscv64FeaturesUniquePtr(new Riscv64InstructionSetFeatures());
}

Riscv64FeaturesUniquePtr Riscv64InstructionSetFeatures::FromCpuInfo() {
  UNIMPLEMENTED(WARNING);
  return FromCppDefines();
}

Riscv64FeaturesUniquePtr Riscv64InstructionSetFeatures::FromHwcap() {
  return Riscv64FeaturesUniquePtr(new Riscv64InstructionSetFeatures());
}

Riscv64FeaturesUniquePtr Riscv64InstructionSetFeatures::FromAssembly() {
  UNIMPLEMENTED(WARNING);
  return FromCppDefines();
}

Riscv64FeaturesUniquePtr Riscv64InstructionSetFeatures::FromCpuFeatures() {
  UNIMPLEMENTED(WARNING);
  return FromCppDefines();
}

bool Riscv64InstructionSetFeatures::Equals(const InstructionSetFeatures* other) const {
  if (InstructionSet::kRiscv64 != other->GetInstructionSet()) {
    return false;
  }
  return true;
}

bool Riscv64InstructionSetFeatures::HasAtLeast(const InstructionSetFeatures* other) const {
  if (InstructionSet::kRiscv64 != other->GetInstructionSet()) {
    return false;
  }
  return true;
}

uint32_t Riscv64InstructionSetFeatures::AsBitmap() const {
  return 0;
}

std::string Riscv64InstructionSetFeatures::GetFeatureString() const {
  std::string result = "gc"; // RV64(IMAFD=G)C
  return result;
}

std::unique_ptr<const InstructionSetFeatures>
Riscv64InstructionSetFeatures::AddFeaturesFromSplitString(
    const std::vector<std::string>&, std::string*) const {
  return std::unique_ptr<const InstructionSetFeatures>(new Riscv64InstructionSetFeatures());
}

std::unique_ptr<const InstructionSetFeatures>
Riscv64InstructionSetFeatures::AddRuntimeDetectedFeatures(
    const InstructionSetFeatures*) const {
  return std::unique_ptr<const InstructionSetFeatures>(new Riscv64InstructionSetFeatures());
}

}  // namespace art

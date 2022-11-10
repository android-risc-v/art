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

#ifndef ART_RUNTIME_ARCH_RISCV64_CALLEE_SAVE_FRAME_RISCV64_H_
#define ART_RUNTIME_ARCH_RISCV64_CALLEE_SAVE_FRAME_RISCV64_H_

#include "arch/instruction_set.h"
#include "base/bit_utils.h"
#include "base/callee_save_type.h"
#include "base/enums.h"
#include "quick/quick_method_frame_info.h"
#include "registers_riscv64.h"
#include "runtime_globals.h"

namespace art {
namespace riscv64 {

static constexpr uint32_t kRiscv64CalleeSaveAlwaysSpills =
    (1 << art::riscv64::RA);  // Return address
static constexpr uint32_t kRiscv64CalleeSaveRefSpills =
    // although SP is callee-saved, it cannot contain object reference, so it is excluded
    // X9/S1 is excluded as it is the ART thread register
    (1 << art::riscv64::FP)  |  // S0
    (1 << art::riscv64::S2)  | (1 << art::riscv64::S3)  | (1 << art::riscv64::S4)  |
    (1 << art::riscv64::S5)  | (1 << art::riscv64::S6)  | (1 << art::riscv64::S7)  |
    (1 << art::riscv64::S8)  | (1 << art::riscv64::S9)  | (1 << art::riscv64::S10) |
    (1 << art::riscv64::S11);
static constexpr uint32_t kRiscv64CalleeSaveAllSpills =
    (1 << art::riscv64::TR);
// X10/A0 is the method pointer. Not saved.
static constexpr uint32_t kRiscv64CalleeSaveArgSpills =
    (1 << art::riscv64::A1) | (1 << art::riscv64::A2) | (1 << art::riscv64::A3) |
    (1 << art::riscv64::A4) | (1 << art::riscv64::A5) | (1 << art::riscv64::A6) |
    (1 << art::riscv64::A7);
static constexpr uint32_t kRiscv64CalleeSaveEverythingSpills =
    // Don't save zero register X0, thread pointer TP and global pointer GP.
    (1 << art::riscv64::SP) |
    (1 << art::riscv64::T0) | (1 << art::riscv64::T1) | (1 << art::riscv64::T2) |
    (1 << art::riscv64::A0) | (1 << art::riscv64::A1) | (1 << art::riscv64::A2) |
    (1 << art::riscv64::A3) | (1 << art::riscv64::A4) | (1 << art::riscv64::A5) |
    (1 << art::riscv64::A6) | (1 << art::riscv64::A7) | (1 << art::riscv64::T3) |
    (1 << art::riscv64::T4) | (1 << art::riscv64::T5) | (1 << art::riscv64::T6) |
    (1 << art::riscv64::TR);

static constexpr uint32_t kRiscv64CalleeSaveFpSpills = 0;
static constexpr uint32_t kRiscv64CalleeSaveFpArgSpills =
    (1 << art::riscv64::F10) | (1 << art::riscv64::F11) | (1 << art::riscv64::F12) |
    (1 << art::riscv64::F13) | (1 << art::riscv64::F14) | (1 << art::riscv64::F15) |
    (1 << art::riscv64::F16) | (1 << art::riscv64::F17);
static constexpr uint32_t kRiscv64CalleeSaveFpAllSpills =
    (1 << art::riscv64::F8)  | (1 << art::riscv64::F9)  |
    (1 << art::riscv64::F18) | (1 << art::riscv64::F19) | (1 << art::riscv64::F20) |
    (1 << art::riscv64::F21) | (1 << art::riscv64::F22) | (1 << art::riscv64::F23) |
    (1 << art::riscv64::F24) | (1 << art::riscv64::F25) | (1 << art::riscv64::F26) |
    (1 << art::riscv64::F27);
static constexpr uint32_t kRiscv64CalleeSaveFpEverythingSpills =
    (1 << art::riscv64::F0)  | (1 << art::riscv64::F1)  | (1 << art::riscv64::F2) |
    (1 << art::riscv64::F3)  | (1 << art::riscv64::F4)  | (1 << art::riscv64::F5) |
    (1 << art::riscv64::F6)  | (1 << art::riscv64::F7)  | (1 << art::riscv64::F8) |
    (1 << art::riscv64::F9)  | (1 << art::riscv64::F10) | (1 << art::riscv64::F11) |
    (1 << art::riscv64::F12) | (1 << art::riscv64::F13) | (1 << art::riscv64::F14) |
    (1 << art::riscv64::F15) | (1 << art::riscv64::F16) | (1 << art::riscv64::F17) |
    (1 << art::riscv64::F18) | (1 << art::riscv64::F19) | (1 << art::riscv64::F20) |
    (1 << art::riscv64::F21) | (1 << art::riscv64::F22) | (1 << art::riscv64::F23) |
    (1 << art::riscv64::F24) | (1 << art::riscv64::F25) | (1 << art::riscv64::F26) |
    (1 << art::riscv64::F27) | (1 << art::riscv64::F28) | (1 << art::riscv64::F29) |
    (1 << art::riscv64::F30) | (1 << art::riscv64::F31);

class Riscv64CalleeSaveFrame {
 public:
  static constexpr uint32_t GetCoreSpills(CalleeSaveType type) {
    type = GetCanonicalCalleeSaveType(type);
    return kRiscv64CalleeSaveAlwaysSpills | kRiscv64CalleeSaveRefSpills |
        (type == CalleeSaveType::kSaveRefsAndArgs ? kRiscv64CalleeSaveArgSpills : 0) |
        (type == CalleeSaveType::kSaveAllCalleeSaves ? kRiscv64CalleeSaveAllSpills : 0) |
        (type == CalleeSaveType::kSaveEverything ? kRiscv64CalleeSaveEverythingSpills : 0);
  }

  static constexpr uint32_t GetFpSpills(CalleeSaveType type) {
    type = GetCanonicalCalleeSaveType(type);
    return kRiscv64CalleeSaveFpSpills |
        (type == CalleeSaveType::kSaveRefsAndArgs ? kRiscv64CalleeSaveFpArgSpills : 0) |
        (type == CalleeSaveType::kSaveAllCalleeSaves ? kRiscv64CalleeSaveFpAllSpills : 0) |
        (type == CalleeSaveType::kSaveEverything ? kRiscv64CalleeSaveFpEverythingSpills : 0);
  }

  static constexpr uint32_t GetFrameSize(CalleeSaveType type) {
    type = GetCanonicalCalleeSaveType(type);
    return RoundUp((POPCOUNT(GetCoreSpills(type)) /* gprs */ +
                    POPCOUNT(GetFpSpills(type)) /* fprs */ +
                    1 /* Method* */) * static_cast<size_t>(kRiscv64PointerSize), kStackAlignment);
  }

  static constexpr QuickMethodFrameInfo GetMethodFrameInfo(CalleeSaveType type) {
    type = GetCanonicalCalleeSaveType(type);
    return QuickMethodFrameInfo(GetFrameSize(type), GetCoreSpills(type), GetFpSpills(type));
  }

  static constexpr size_t GetFpr1Offset(CalleeSaveType type) {
    type = GetCanonicalCalleeSaveType(type);
    return GetFrameSize(type) -
           (POPCOUNT(GetCoreSpills(type)) +
            POPCOUNT(GetFpSpills(type))) * static_cast<size_t>(kRiscv64PointerSize);
  }

  static constexpr size_t GetGpr1Offset(CalleeSaveType type) {
    type = GetCanonicalCalleeSaveType(type);
    return GetFrameSize(type) -
           POPCOUNT(GetCoreSpills(type)) * static_cast<size_t>(kRiscv64PointerSize);
  }

  static constexpr size_t GetReturnPcOffset(CalleeSaveType type) {
    type = GetCanonicalCalleeSaveType(type);
    return GetFrameSize(type) - static_cast<size_t>(kRiscv64PointerSize);
  }
};

static_assert(Riscv64CalleeSaveFrame::GetFrameSize(CalleeSaveType::kSaveRefsAndArgs) == 224);
static_assert(Riscv64CalleeSaveFrame::GetFrameSize(CalleeSaveType::kSaveAllCalleeSaves) == 208);
static_assert(Riscv64CalleeSaveFrame::GetFrameSize(CalleeSaveType::kSaveEverything) == 496);

}  // namespace riscv64
}  // namespace art

#endif  // ART_RUNTIME_ARCH_RISCV64_CALLEE_SAVE_FRAME_RISCV64_H_

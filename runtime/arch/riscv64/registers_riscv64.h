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

#ifndef ART_RUNTIME_ARCH_X86_64_REGISTERS_RISCV64_H_
#define ART_RUNTIME_ARCH_X86_64_REGISTERS_RISCV64_H_

//#include <iosfwd>

#include "base/macros.h"

namespace art {
namespace riscv64 {

enum XRegister {
  Zero = 0,  // X0
  RA   = 1,  // X1, return address
  SP   = 2,  // X2, stack pointer
  GP   = 3,  // X3, global pointer
  TP   = 4,  // X4, thread pointer (points to TLS area, not ART-internal thread)

  T0   = 5,  // X5, temporary 0
  T1   = 6,  // X6, temporary 1
  T2   = 7,  // X7, temporary 2

  FP   = 8,  // X8/S0, callee-saved 0 / frame pointer
  S1   = 9,  // X9, callee-saved 1

  A0   = 10, // X10, argument 0 / return value 0
  A1   = 11, // X11, argument 1 / return value 1
  A2   = 12, // X12, argument 2
  A3   = 13, // X13, argument 3
  A4   = 14, // X14, argument 4
  A5   = 15, // X15, argument 5
  A6   = 16, // X16, argument 6
  A7   = 17, // X17, argument 7

  S2   = 18, // X18, callee-saved 2
  S3   = 19, // X19, callee-saved 3
  S4   = 20, // X20, callee-saved 4
  S5   = 21, // X21, callee-saved 5
  S6   = 22, // X22, callee-saved 6
  S7   = 23, // X23, callee-saved 7
  S8   = 24, // X24, callee-saved 8
  S9   = 25, // X25, callee-saved 9
  S10  = 26, // X26, callee-saved 10
  S11  = 27, // X27, callee-saved 11

  T3   = 28,  // X28, temporary 3
  T4   = 29,  // X29, temporary 4
  T5   = 30,  // X30, temporary 5
  T6   = 31,  // X31, temporary 6

  kNumberOfXRegisters = 32,
  kNoRegister = -1,  // Signals an illegal register.

  // Aliases.
  TR = S1, // ART Thread Register - managed runtime
};

//std::ostream& operator<<(std::ostream& os, const Register& rhs);

enum FloatRegister {
  F0  = 0,
  F1  = 1,
  F2  = 2,
  F3  = 3,
  F4  = 4,
  F5  = 5,
  F6  = 6,
  F7  = 7,
  F8  = 8,
  F9  = 9,
  F10 = 10,
  F11 = 11,
  F12 = 12,
  F13 = 13,
  F14 = 14,
  F15 = 15,
  F16 = 16,
  F17 = 17,
  F18 = 18,
  F19 = 19,
  F20 = 20,
  F21 = 21,
  F22 = 22,
  F23 = 23,
  F24 = 24,
  F25 = 25,
  F26 = 26,
  F27 = 27,
  F28 = 28,
  F29 = 29,
  F30 = 30,
  F31 = 31,
  kNumberOfFRegisters = 32,
};

//std::ostream& operator<<(std::ostream& os, const FloatRegister& rhs);

}  // namespace riscv64
}  // namespace art

#endif  // ART_RUNTIME_ARCH_X86_64_REGISTERS_RISCV64_H_

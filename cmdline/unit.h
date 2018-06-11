/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef ART_CMDLINE_UNIT_H_
#define ART_CMDLINE_UNIT_H_

namespace art {

// Used for arguments that simply indicate presence (e.g. "-help") without any values.
struct Unit {
  // Historical note: We specified a user-defined constructor to avoid
  // 'Conditional jump or move depends on uninitialised value(s)' errors
  // when running Valgrind.
  Unit() {}
  Unit(const Unit&) = default;
  ~Unit() {}
  bool operator==(Unit) const {
    return true;
  }
};

}  // namespace art

#endif  // ART_CMDLINE_UNIT_H_

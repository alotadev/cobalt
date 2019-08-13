// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef COBALT_SRC_LIB_CRYPTO_UTIL_ERRORS_H_
#define COBALT_SRC_LIB_CRYPTO_UTIL_ERRORS_H_

#include <string>

namespace cobalt {
namespace crypto {

// Returns the error message corresponding to the last error from the
// this crypto library that occurred on this thread. After
// each call to a method from this library the result code should be
// checked. A result of one indicates success and zero indicates failure.
// When a zero is returned this function may be invoked to obtain a
// human-readable description of the error.
std::string GetLastErrorMessage();

}  // namespace crypto

}  // namespace cobalt

#endif  // COBALT_SRC_LIB_CRYPTO_UTIL_ERRORS_H_

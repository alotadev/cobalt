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

#ifndef COBALT_ANALYZER_STORE_STORE_H_
#define COBALT_ANALYZER_STORE_STORE_H_

#include <string>

namespace cobalt {
namespace analyzer {

// Interface to a key value store.
class Store {
 public:
  // Returns 0 on success.
  virtual int put(const std::string& key, const std::string& val) = 0;

  // Returns 0 on success.
  virtual int get(const std::string& key, std::string* out) = 0;
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_STORE_H_

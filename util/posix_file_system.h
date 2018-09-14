// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_UTIL_POSIX_FILE_SYSTEM_H_
#define COBALT_UTIL_POSIX_FILE_SYSTEM_H_

#include <string>
#include <vector>

#include "util/file_system.h"

namespace cobalt {
namespace util {

// PosixFileSystem implements FileSystem for posix compliant systems.
class PosixFileSystem : public FileSystem {
 public:
  bool MakeDirectory(const std::string &directory) override;
  tensorflow_statusor::StatusOr<std::vector<std::string>> ListFiles(
      const std::string &directory) override;
  bool Delete(const std::string &file) override;
  tensorflow_statusor::StatusOr<size_t> FileSize(
      const std::string &file) override;
  bool Rename(const std::string &from, const std::string &to) override;
};

}  // namespace util
}  // namespace cobalt

#endif  // COBALT_UTIL_POSIX_FILE_SYSTEM_H_

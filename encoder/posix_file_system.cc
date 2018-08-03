// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <sstream>

#include "encoder/posix_file_system.h"

namespace cobalt {
namespace encoder {

using tensorflow_statusor::StatusOr;
using util::Status;
using util::StatusCode;

bool PosixFileSystem::MakeDirectory(const std::string &directory) {
  return mkdir(directory.c_str(), S_IRWXU | S_IRWXG | S_IRWXO) == 0;
}

StatusOr<std::vector<std::string>> PosixFileSystem::ListFiles(
    const std::string &directory) {
  std::vector<std::string> files;
  DIR *dir;
  struct dirent *ent;
  if ((dir = opendir(directory.c_str())) != nullptr) {
    while ((ent = readdir(dir)) != nullptr) {
      if (ent->d_name[0] == '.' &&
          (ent->d_name[1] == '.' || ent->d_name[1] == '\0')) {
        continue;
      }
      files.push_back(ent->d_name);
    }
  } else {
    std::stringstream ss;
    ss << "Unable to open directory [" << directory << "]: " << errno;
    return Status(StatusCode::INTERNAL, ss.str());
  }

  return files;
}

bool PosixFileSystem::Delete(const std::string &file) {
  return std::remove(file.c_str()) == 0;
}

StatusOr<size_t> PosixFileSystem::FileSize(const std::string &file) {
  struct stat st;
  if (stat(file.c_str(), &st) != 0) {
    std::stringstream ss;
    ss << "Unable to stat file [" << file << "]: " << errno;
    return Status(StatusCode::INTERNAL, ss.str());
  }
  return st.st_size;
}

bool PosixFileSystem::Rename(const std::string &from, const std::string &to) {
  return std::rename(from.c_str(), to.c_str()) == 0;
}

}  // namespace encoder
}  // namespace cobalt

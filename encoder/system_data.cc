// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "encoder/system_data.h"

#include <cstring>
#include <utility>

#include "./logging.h"

namespace cobalt {
namespace encoder {

namespace {

#if defined(__x86_64__)

// This identifies board names for x86 Systems.
// If the signature of the CPU matches a known signature, then we use the name,
// otherwise we encode the signature as a string so we can easily identify when
// new signatures start to become popular.
std::string getBoardName(int signature) {
  // This function will only be run once per system boot, so this map will only
  // be created once.
  std::map<int, std::string> knownCPUSignatures = {
      {0x806e9, "Eve"},
  };

  auto name = knownCPUSignatures.find(signature);
  if (name == knownCPUSignatures.end()) {
    char sigstr[20];
    sprintf(sigstr, "unknown:0x%X", signature);
    return sigstr;
  } else {
    return name->second;
  }
}

// Invokes the cpuid instruction on X86. |info_type| specifies which query
// we are performing. This is written into register EAX prior to invoking
// cpuid. (The sub-type specifier in register ECX is alwyas set to zero.)  The
// results from registers EAX, EBX, ECX, EDX respectively are writtent into the
// four entries of |cpu_info|. See for example the wikipedia article on
// cpuid for more info.
void Cpuid(int info_type, int cpu_info[4]) {
  __asm__ volatile("cpuid\n"
                   : "=a"(cpu_info[0]), "=b"(cpu_info[1]), "=c"(cpu_info[2]),
                     "=d"(cpu_info[3])
                   : "a"(info_type), "c"(0));
}

// Invokes Cpuid() to determine the board_name.
void PopulateBoardName(SystemProfile& profile) {
  // First we invoke Cpuid with info_type = 0 in order to obtain num_ids
  // and vendor_name.
  int cpu_info[4] = {-1};
  Cpuid(0, cpu_info);
  int num_ids = cpu_info[0];

  if (num_ids > 0) {
    // Then invoke Cpuid again with info_type = 1 in order to obtain
    // |signature|.
    Cpuid(1, cpu_info);
    profile.set_board_name(getBoardName(cpu_info[0]));
  }
}

#endif

}  // namespace

SystemData::SystemData() { PopulateSystemProfile(); }

void SystemData::PopulateSystemProfile() {
#if defined(__linux__)

  system_profile_.set_os(SystemProfile::LINUX);

#elif defined(__Fuchsia__)

  system_profile_.set_os(SystemProfile::FUCHSIA);

#else

  system_profile_.set_os(SystemProfile::UNKNOWN_OS);

#endif

#if defined(__x86_64__)

  system_profile_.set_arch(SystemProfile::X86_64);
  PopulateBoardName(system_profile_);

#elif defined(__aarch64__)

  system_profile_.set_arch(SystemProfile::ARM_64);
  // TODO(rudominer) Implement CpuInfo on ARM.

#else

  system_profile_.set_arch(SystemProfile::UNKNOWN_ARCH);

#endif
}

}  // namespace encoder
}  // namespace cobalt

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ENCODER_SYSTEM_DATA_H_
#define COBALT_ENCODER_SYSTEM_DATA_H_

#include <string>

#include <mutex>
#include <vector>
#include <utility>

#include "./experiment.pb.h"
#include "./observation_batch.pb.h"
#include "third_party/abseil-cpp/absl/synchronization/mutex.h"

namespace cobalt {
namespace encoder {

// An abstraction of the interface to SystemData that allows mocking in
// tests.
class SystemDataInterface {
 public:
  virtual ~SystemDataInterface() = default;

  // Returns the SystemProfile for the current system.
  virtual const SystemProfile& system_profile() const = 0;

  // Returns a vector with all experiments the system has a notion of.
  virtual const std::vector<Experiment>& experiments() const = 0;
};

// The Encoder client creates a singleton instance of SystemData at start-up
// time and uses it to query data about the client's running system. There
// are two categories of data: static data about the system encapsulated in
// the SystemProfile, and dynamic stateful data about the running system.
class SystemData : public SystemDataInterface {
 public:
  // Constructor: Populuates system_profile_ with the real SystemProfile
  // of the actual running system and the specified product name.
  explicit SystemData(const std::string& product_name);

  virtual ~SystemData() = default;

  // Returns a vector with all experiments the system has a notion of.
  const std::vector<Experiment>& experiments() const override
      SHARED_LOCKS_REQUIRED(experiments_mutex_) {
    return experiments_;
  }

  // Returns the SystemProfile for the current system.
  const SystemProfile& system_profile() const override {
    return system_profile_;
  }

  // Resets the experiment state to the one provided.
  void SetExperimentState(std::vector<Experiment> experiments)
      EXCLUSIVE_LOCKS_REQUIRED(experiments_mutex_) {
    experiments_ = std::move(experiments);
  }

  // Overrides the stored SystemProfile. Useful for testing.
  void OverrideSystemProfile(const SystemProfile& profile);

 private:
  void PopulateSystemProfile();

  SystemProfile system_profile_;
  absl::Mutex experiments_mutex_;
  std::vector<Experiment> experiments_ GUARDED_BY(experiments_mutex_);
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_SYSTEM_DATA_H_

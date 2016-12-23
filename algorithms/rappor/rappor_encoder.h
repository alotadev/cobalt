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

#ifndef COBALT_ALGORITHMS_RAPPOR_RAPPOR_ENCODER_H_
#define COBALT_ALGORITHMS_RAPPOR_RAPPOR_ENCODER_H_

#include <memory>
#include <string>
#include <utility>

#include "./observation.pb.h"
#include "algorithms/rappor/rappor_config_validator.h"
#include "config/encodings.pb.h"
#include "encoder/client_secret.h"
#include "util/crypto_util/random.h"

namespace cobalt {
namespace rappor {

namespace testing {
// Forward declaration of a test in another namespace is necessary in order
// to make the test a friend class.
class BasicRapporDeterministicTest;
}  // namespace testing

enum Status {
  kOK = 0,
  kInvalidConfig,
  kInvalidInput,
};

// Performs String RAPPOR encoding.
class RapporEncoder {
 public:
  // Constructor.
  // The |client_secret| is used to determine the cohort and the PRR.
  RapporEncoder(const RapporConfig& config,
                encoder::ClientSecret client_secret);
  virtual ~RapporEncoder();

  // Encodes |value| using RAPPOR encoding. Returns kOK on success, or
  // kInvalidConfig if the |config| passed to the constructor is not valid.
  Status Encode(const ValuePart& value, RapporObservation* observation_out);

 private:
  // Allows Friend classess to set a special RNG for use in tests.
  void SetRandomForTesting(std::unique_ptr<crypto::Random> random) {
    random_ = std::move(random);
  }

  std::unique_ptr<RapporConfigValidator> config_;
  std::unique_ptr<crypto::Random> random_;
  encoder::ClientSecret client_secret_;
};

// Performs encoding for Basic RAPPOR, a.k.a Categorical RAPPOR. No cohorts
// are used and the list of all candidates must be pre-specified as part
// of the BasicRapporConfig.
// The |client_secret| is used to determine the PRR.
class BasicRapporEncoder {
 public:
  BasicRapporEncoder(const BasicRapporConfig& config,
                     encoder::ClientSecret client_secret);
  ~BasicRapporEncoder();

  // Encodes |value| using Basic RAPPOR encoding. |value| must be one
  // of the categories listed in the |categories| field of the |config|
  // that was passed to the constructor. Returns kOK on success, kInvalidConfig
  // if the |config| passed to the constructor is not valid, and kInvalidInput
  // if |value| is not one of the |categories|.
  Status Encode(const ValuePart& value,
                BasicRapporObservation* observation_out);

 private:
  friend class BasicRapporAnalyzerTest;
  friend class testing::BasicRapporDeterministicTest;

  // Allows Friend classess to set a special RNG for use in tests.
  void SetRandomForTesting(std::unique_ptr<crypto::Random> random) {
    random_ = std::move(random);
  }

  std::unique_ptr<RapporConfigValidator> config_;
  std::unique_ptr<crypto::Random> random_;
  encoder::ClientSecret client_secret_;
};

}  // namespace rappor
}  // namespace cobalt

#endif  // COBALT_ALGORITHMS_RAPPOR_RAPPOR_ENCODER_H_

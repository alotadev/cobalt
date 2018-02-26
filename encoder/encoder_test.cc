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

#include "encoder/encoder.h"

#include <utility>

#include "./gtest.h"
#include "./logging.h"
#include "config/config_text_parser.h"
#include "encoder/client_secret.h"
#include "encoder/project_context.h"
#include "third_party/gflags/include/gflags/gflags.h"

namespace cobalt {
namespace encoder {

using config::EncodingRegistry;
using config::MetricRegistry;

namespace {

const uint32_t kCustomerId = 1;
const uint32_t kProjectId = 1;

// This unix timestamp corresponds to Friday Dec 2, 2016 in UTC
// and Thursday Dec 1, 2016 in Pacific time.
const time_t kSomeTimestamp = 1480647356;
// This is the day index for Friday Dec 2, 2016
const uint32_t kUtcDayIndex = 17137;
// This is the day index for Thurs Dec 1, 2016
const uint32_t kPacificDayIndex = 17136;

const char* kMetricConfigText = R"(
# Metric 1 has one string part, and local time_zone_policy.
element {
  customer_id: 1
  project_id: 1
  id: 1
  time_zone_policy: LOCAL
  parts {
    key: "Part1"
    value {
    }
  }
}

# Metric 2 has one integer part, and UTC time_zone_policy.
element {
  customer_id: 1
  project_id: 1
  id: 2
  time_zone_policy: UTC
  parts {
    key: "Part1"
    value {
      data_type: INT
    }
  }
}


# Metric 3 has one blob part, and local time_zone_policy.
element {
  customer_id: 1
  project_id: 1
  id: 3
  time_zone_policy: LOCAL
  parts {
    key: "Part1"
    value {
      data_type: BLOB
    }
  }
}

# Metric 4 has one String part and one int part, and UTC time_zone_policy.
element {
  customer_id: 1
  project_id: 1
  id: 4
  time_zone_policy: UTC
  parts {
    key: "city"
    value {
    }
  }
  parts {
    key: "rating"
    value {
      data_type: INT
    }
  }
}

# Metric 5 is missing a time_zone_policy
element {
  customer_id: 1
  project_id: 1
  id: 5
  parts {
    key: "Part1"
    value {
    }
  }
}

# Metric 6 has an INDEX part.
element {
  customer_id: 1
  project_id: 1
  id: 6
  time_zone_policy: UTC
  parts {
    key: "Part1"
    value {
      data_type: INDEX
    }
  }
}

# Metric 7 has a DOUBLE part.
element {
  customer_id: 1
  project_id: 1
  id: 7
  time_zone_policy: UTC
  parts {
    key: "Part1"
    value {
      data_type: DOUBLE
    }
  }
}

# Metric 8 has an int distribution part.
element {
  customer_id: 1
  project_id: 1
  id: 8
  time_zone_policy: UTC
  parts {
    key: "Part1"
    value {
      data_type: INT
      int_buckets: {
        linear: {
          floor: 0
          num_buckets: 10
          step_size: 5
        }
      }
    }
  }
}

)";

const char* kEncodingConfigText = R"(
# EncodingConfig 1 is Forculus.
element {
  customer_id: 1
  project_id: 1
  id: 1
  forculus {
    threshold: 20
  }
}

# EncodingConfig 2 is String RAPPOR.
element {
  customer_id: 1
  project_id: 1
  id: 2
  rappor {
    num_bloom_bits: 8
    num_hashes: 2
    num_cohorts: 20
    prob_0_becomes_1: 0.25
    prob_1_stays_1: 0.75
  }
}

# EncodingConfig 3 is Basic RAPPOR with string categories.
element {
  customer_id: 1
  project_id: 1
  id: 3
  basic_rappor {
    prob_0_becomes_1: 0.25
    prob_1_stays_1: 0.75
    string_categories: {
      category: "Apple"
      category: "Banana"
      category: "Cantaloupe"
    }
  }
}

# EncodingConfig 4 is Basic RAPPOR with int categories.
element {
  customer_id: 1
  project_id: 1
  id: 4
  basic_rappor {
    prob_0_becomes_1: 0.25
    prob_1_stays_1: 0.75
    int_range_categories: {
      first: 123
      last:  234
    }
  }
}

# EncodingConfig 5 is Forculus with a missing threshold.
element {
  customer_id: 1
  project_id: 1
  id: 5
  forculus {
  }
}

# EncodingConfig 6 is String RAPPOR with many missing values.
element {
  customer_id: 1
  project_id: 1
  id: 6
  rappor {
  }
}

# EncodingConfig 7 is the NoOp encoding.
element {
  customer_id: 1
  project_id: 1
  id: 7
  no_op_encoding {
  }
}

# EncodingConfig 8 is Basic RAPPOR with 5 INDEXed categories.
element {
  customer_id: 1
  project_id: 1
  id: 8
  basic_rappor {
    prob_0_becomes_1: 0.25
    prob_1_stays_1: 0.75
    indexed_categories: {
      num_categories: 5,
    }
  }
}

)";

// Returns a ProjectContext obtained by parsing the above configuration
// text strings.
std::shared_ptr<ProjectContext> GetTestProject() {
  // Parse the metric config string
  auto metric_parse_result =
      config::FromString<RegisteredMetrics>(kMetricConfigText, nullptr);
  EXPECT_EQ(config::kOK, metric_parse_result.second);
  std::shared_ptr<MetricRegistry> metric_registry(
      metric_parse_result.first.release());

  // Parse the encoding config string
  auto encoding_parse_result =
      config::FromString<RegisteredEncodings>(kEncodingConfigText, nullptr);
  EXPECT_EQ(config::kOK, encoding_parse_result.second);
  std::shared_ptr<EncodingRegistry> encoding_registry(
      encoding_parse_result.first.release());

  return std::shared_ptr<ProjectContext>(new ProjectContext(
      kCustomerId, kProjectId, metric_registry, encoding_registry));
}

}  // namespace

// Checks |result|: Checks that the status is kOK, that the observation and
// metadata are not null, that the observation has a single part named
// "Part1", that it uses the expected encoding and that it is not empty.
// |expect_utc| should be true to indicate that it is expected that the
// day index was computed using UTC.
void CheckSinglePartResult(
    const Encoder::Result& result, uint32_t expected_metric_id,
    uint32_t expected_encoding_config_id, bool expect_utc,
    const ObservationPart::ValueCase& expected_encoding) {
  ASSERT_EQ(Encoder::kOK, result.status);
  ASSERT_NE(nullptr, result.observation);
  ASSERT_NE(nullptr, result.metadata);
  EXPECT_EQ(kCustomerId, result.metadata->customer_id());
  EXPECT_EQ(kProjectId, result.metadata->project_id());
  EXPECT_EQ(expected_metric_id, result.metadata->metric_id());
  if (expect_utc) {
    EXPECT_EQ(kUtcDayIndex, result.metadata->day_index());
  } else {
    // Only perform the following check when running this test in the Pacific
    // timezone. Note that |timezone| is a global variable defined in <ctime>
    // that stores difference between UTC and the latest local standard time, in
    // seconds west of UTC. This value is not adjusted for daylight saving.
    // See https://www.gnu.org/software/libc/manual/html_node/ \
    //                              Time-Zone-Functions.html#Time-Zone-Functions
    if (timezone / 3600 == 8) {
      EXPECT_EQ(kPacificDayIndex, result.metadata->day_index());
    }
  }

  const Observation& observation = *result.observation;
  // The Metric specified has only one part named "Part1" so the encoded
  // observation should have one part named "Part1".
  ASSERT_EQ(1, observation.parts_size());
  const ObservationPart& obs_part = observation.parts().at("Part1");

  // The observation part should use the right encoding.
  EXPECT_EQ(expected_encoding_config_id, obs_part.encoding_config_id());
  EXPECT_EQ(expected_encoding, obs_part.value_case());

  // We sanity test the Observation by checking that it is not empty.
  switch (expected_encoding) {
    case ObservationPart::kForculus: {
      EXPECT_NE("", obs_part.forculus().ciphertext());
      break;
    }
    case ObservationPart::kRappor: {
      EXPECT_NE("", obs_part.rappor().data());
      break;
    }
    case ObservationPart::kBasicRappor: {
      EXPECT_NE("", obs_part.basic_rappor().data());
      break;
    }
    case ObservationPart::kUnencoded: {
      EXPECT_TRUE(obs_part.unencoded().has_unencoded_value());
      break;
    }
    default:
      EXPECT_TRUE(false) << " Unexpected case";
  }
}

// Tests the EncodeString() method using the given |value| and the given
// metric and encoding. The metric is expected to have a single part named
// "Part1". We validate that there are no errors and that the
// produced Observation has the |expected_type| and is non-empty.
// Returns the encoded Observation.
Observation DoEncodeStringTest(
    std::string value, uint32_t metric_id, uint32_t encoding_config_id,
    bool expect_utc, const ObservationPart::ValueCase& expected_encoding) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();

  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret());
  // Set a static current time so we can test the day_index computation.
  encoder.set_current_time(kSomeTimestamp);

  // Encode an observation for the given metric and encoding. The metric is
  // expected to have a single part.
  Encoder::Result result =
      encoder.EncodeString(metric_id, encoding_config_id, value);

  CheckSinglePartResult(result, metric_id, encoding_config_id, expect_utc,
                        expected_encoding);

  return *result.observation;
}

// Tests the EncodeInt() method using the given |value| and the given
// metric and encoding. The metric is expected to have a single part named
// "Part1". The encoding is expected to be for Basic RAPPOR.
// We validate that there are no errors and that the
// produced Observation has the |expected_type| and is non-empty.
// Returns the encoded Observation.
Observation DoEncodeIntTest(
    int64_t value, uint32_t metric_id, uint32_t encoding_config_id,
    bool expect_utc, const ObservationPart::ValueCase& expected_encoding) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();

  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret());
  // Set a static current time so we can test the day_index computation.
  encoder.set_current_time(kSomeTimestamp);

  // Encode an observation for the given metric and encoding. The metric is
  // expected to have a single part.
  Encoder::Result result =
      encoder.EncodeInt(metric_id, encoding_config_id, value);

  CheckSinglePartResult(result, metric_id, encoding_config_id, expect_utc,
                        expected_encoding);
  return *result.observation;
}

// Tests the EncodeDouble() method using the given |value| and the given
// metric and encoding. The metric is expected to have a single part named
// "Part1".
//
// If expectOK is true then we verify that there are no errors and that the
// produced Observation has the |expected_type| and is non-empty. Otherwise
// we verify that kInvalidArguments is returned.
Observation DoEncodeDoubleTest(
    bool expectOK, double value, uint32_t metric_id,
    uint32_t encoding_config_id, bool expect_utc,
    const ObservationPart::ValueCase& expected_encoding) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();

  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret());
  // Set a static current time so we can test the day_index computation.
  encoder.set_current_time(kSomeTimestamp);

  // Encode an observation for the given metric and encoding. The metric is
  // expected to have a single part.
  Encoder::Result result =
      encoder.EncodeDouble(metric_id, encoding_config_id, value);

  if (expectOK) {
    CheckSinglePartResult(result, metric_id, encoding_config_id, expect_utc,
                          expected_encoding);
  } else {
    EXPECT_EQ(Encoder::kInvalidArguments, result.status)
        << "encoding_config_id=" << encoding_config_id;
  }

  return *result.observation;
}

// Tests the EncodeIndex() method using the given |index| and the given
// metric and encoding. The metric is expected to have a single part named
// "Part1".
//
// If expectOK is true then we verify that there are no errors and that the
// produced Observation has the |expected_type| and is non-empty. Otherwise
// we verify that kInvalidArguments is returned.
void DoEncodeIndexTest(bool expectOK, uint32_t index, uint32_t metric_id,
                       uint32_t encoding_config_id, bool expect_utc,
                       const ObservationPart::ValueCase& expected_encoding) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();

  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret());
  // Set a static current time so we can test the day_index computation.
  encoder.set_current_time(kSomeTimestamp);

  // Encode an observation for the given metric and encoding. The metric is
  // expected to have a single part.
  Encoder::Result result =
      encoder.EncodeIndex(metric_id, encoding_config_id, index);

  if (expectOK) {
    CheckSinglePartResult(result, metric_id, encoding_config_id, expect_utc,
                          expected_encoding);
  } else {
    EXPECT_EQ(Encoder::kInvalidArguments, result.status)
        << "encoding_config_id=" << encoding_config_id;
  }
}

// Tests the EncodeBlob() method using the given |value| and the given
// metric and encoding. The metric is expected to have a single part named
// "Part1". The encoding is expected to be for Forculus.
// We validate that there are no errors and that the
// produced Observation has the |expected_type| and is non-empty.
// Returns the encoded Observation.
Observation DoEncodeBlobTest(
    const void* data, size_t num_bytes, uint32_t metric_id,
    uint32_t encoding_config_id, bool expect_utc,
    const ObservationPart::ValueCase& expected_encoding) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();

  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret());
  // Set a static current time so we can test the day_index computation.
  encoder.set_current_time(kSomeTimestamp);

  // Encode an observation for the given metric and encoding. The metric is
  // expected to have a single part.
  Encoder::Result result =
      encoder.EncodeBlob(metric_id, encoding_config_id, data, num_bytes);

  CheckSinglePartResult(result, metric_id, encoding_config_id, expect_utc,
                        expected_encoding);
  return *result.observation;
}

// Tests the EncodeIntBucketDistribution() method using the given |distribution|
// and the given metric and encoding. The metric is expected to have a single
// part named "Part1". The encoding is expected to be NoOp.
// Returns the encoded Observation.
//
// If expectOK is true then we verify that there are no errors and that the
// produced Observation has the |expected_type| and is non-empty. Otherwise
// we verify that kInvalidArguments is returned.
Observation DoEncodeIntBucketDistributionTest(
    bool expect_ok, const std::map<uint32_t, uint64_t>& distribution,
    uint32_t metric_id, uint32_t encoding_config_id, bool expect_utc,
    const ObservationPart::ValueCase& expected_encoding) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();

  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret());
  // Set a static current time so we can test the day_index computation.
  encoder.set_current_time(kSomeTimestamp);

  // Encode an observation for the given metric and encoding. The metric is
  // expected to have a single part.
  Encoder::Result result = encoder.EncodeIntBucketDistribution(
      metric_id, encoding_config_id, distribution);

  if (expect_ok) {
    CheckSinglePartResult(result, metric_id, encoding_config_id, expect_utc,
                          expected_encoding);
  } else {
    EXPECT_EQ(Encoder::kInvalidArguments, result.status)
        << "encoding_config_id=" << encoding_config_id;
  }
  return *result.observation;
}

// Tests EncodeString() with Forculus as the specified encoding.
TEST(EncoderTest, EncodeStringForculus) {
  // Metric 1 has a single string part.
  // EncodingConfig 1 is Forculus.
  DoEncodeStringTest("some value", 1, 1, false, ObservationPart::kForculus);
}

// Tests EncodeString() with String RAPPOR as the specified encoding.
TEST(EncoderTest, EncodeStringRappor) {
  // Metric 1 has a single string part.
  // EncodingConfig 2 is String RAPPOR
  DoEncodeStringTest("some value", 1, 2, false, ObservationPart::kRappor);
}

// Tests EncodeString() with Basic RAPPOR as the specified encoding.
TEST(EncoderTest, EncodeStringBasicRappor) {
  // Metric 1 has a single string part.
  // EncodingConfig 3 is Basic RAPPOR with string values. Here we need the
  // value to be one of the categories.
  DoEncodeStringTest("Apple", 1, 3, false, ObservationPart::kBasicRappor);
}

// Tests EncodeString() with NoOp as the specified encoding.
TEST(EncoderTest, EncodeStringNoOp) {
  // Metric 1 has a single string part.
  // EncodingConfig 7 is NoOp.
  auto obs = DoEncodeStringTest("some value", 1, 7, false,
                                ObservationPart::kUnencoded);

  EXPECT_EQ(
      "some value",
      obs.parts().at("Part1").unencoded().unencoded_value().string_value());
}

// Tests EncodeInt() with Basic RAPPOR as the specified encoding.
TEST(EncoderTest, EncodeIntBasicRappor) {
  // Metric 2 has a single integer part.
  // EncodingConfig 4 is Basic RAPPOR with int values. Here we need the value
  // to be one of the categories.
  DoEncodeIntTest(125, 2, 4, true, ObservationPart::kBasicRappor);
}

// Tests the EncodeIndex() method with both valid and invalid inputs.
TEST(EncoderTest, EncodeIndex) {
  // Metric 6 has a single part of type INDEX.
  // EncodingConfig 8 is Basic RAPPOR with five INDEXed categories.
  bool expect_ok = true;
  uint32_t index = 0;
  bool expect_utc = true;
  DoEncodeIndexTest(expect_ok, index, 6, 8, expect_utc,
                    ObservationPart::kBasicRappor);
  index = 1;
  DoEncodeIndexTest(expect_ok, index, 6, 8, expect_utc,
                    ObservationPart::kBasicRappor);
  index = 4;
  DoEncodeIndexTest(expect_ok, index, 6, 8, expect_utc,
                    ObservationPart::kBasicRappor);

  // Index 5 should yield kInalidArgs.
  expect_ok = false;
  index = 5;
  DoEncodeIndexTest(expect_ok, index, 6, 8, expect_utc,
                    ObservationPart::kBasicRappor);

  // Reset to index 0 just to confirm it still succeeds.
  expect_ok = true;
  index = 0;
  DoEncodeIndexTest(expect_ok, index, 6, 8, expect_utc,
                    ObservationPart::kBasicRappor);

  // Now we switch to metric 1 which has one string part. That should fail.
  expect_ok = false;
  DoEncodeIndexTest(expect_ok, index, 1, 8, expect_utc,
                    ObservationPart::kBasicRappor);

  // Now we switch to metric 2 which has one int part. That should fail.
  DoEncodeIndexTest(expect_ok, index, 2, 8, expect_utc,
                    ObservationPart::kBasicRappor);

  // Now we switch to metric 3 which has one blob part. That should fail.
  DoEncodeIndexTest(expect_ok, index, 3, 8, expect_utc,
                    ObservationPart::kBasicRappor);

  // Now we switch to metric 7 which has one double part. That should fail.
  DoEncodeIndexTest(expect_ok, index, 7, 8, expect_utc,
                    ObservationPart::kBasicRappor);

  // Reset to metric 6 just to confirm it still succeeds.
  expect_ok = true;
  DoEncodeIndexTest(expect_ok, index, 6, 8, expect_utc,
                    ObservationPart::kBasicRappor);

  // Now we switch to encoding 1 which is Forculus. That should fail.
  expect_ok = false;
  DoEncodeIndexTest(expect_ok, index, 6, 1, expect_utc,
                    ObservationPart::kForculus);

  // Now we switch to encoding 2 which is String RAPPOR. That should fail.
  DoEncodeIndexTest(expect_ok, index, 6, 2, expect_utc,
                    ObservationPart::kRappor);

  // Now we switch to encoding 3 which is Basic RAPPOR with string categories.
  // That should fail.
  DoEncodeIndexTest(expect_ok, index, 6, 3, expect_utc,
                    ObservationPart::kBasicRappor);

  // Now we switch to encoding 4 which is Basic RAPPOR with int categories.
  // That should fail.
  DoEncodeIndexTest(expect_ok, index, 6, 4, expect_utc,
                    ObservationPart::kBasicRappor);

  // Now we switch to encoding 7 which is NoOpEncoding. That should be OK.
  expect_ok = true;
  DoEncodeIndexTest(expect_ok, index, 6, 7, expect_utc,
                    ObservationPart::kUnencoded);
}

// Tests the EncodeDouble() method with both valid and invalid inputs.
TEST(EncoderTest, EncodeDouble) {
  // Metric 7 has a single part of type DOUBLE.
  // EncodingConfig 7 is NoOp.
  bool expect_ok = true;
  double value = 3.14159;
  bool expect_utc = true;
  DoEncodeDoubleTest(expect_ok, value, 7, 7, expect_utc,
                     ObservationPart::kUnencoded);

  // Now we switch to metric 1 which has one string part. That should fail.
  expect_ok = false;
  DoEncodeDoubleTest(expect_ok, value, 1, 7, expect_utc,
                     ObservationPart::kUnencoded);

  // Now we switch to metric 2 which has one int part. That should fail.
  DoEncodeDoubleTest(expect_ok, value, 2, 7, expect_utc,
                     ObservationPart::kUnencoded);

  // Now we switch to metric 3 which has one blob part. That should fail.
  DoEncodeDoubleTest(expect_ok, value, 3, 7, expect_utc,
                     ObservationPart::kUnencoded);

  // Reset to metric 7 just to confirm it still succeeds.
  expect_ok = true;
  DoEncodeDoubleTest(expect_ok, value, 7, 7, expect_utc,
                     ObservationPart::kUnencoded);

  // Now we switch to encoding 1 which is Forculus. That should fail.
  expect_ok = false;
  DoEncodeDoubleTest(expect_ok, value, 7, 1, expect_utc,
                     ObservationPart::kForculus);

  // Now we switch to encoding 2 which is String RAPPOR. That should fail.
  DoEncodeDoubleTest(expect_ok, value, 7, 2, expect_utc,
                     ObservationPart::kRappor);

  // Now we switch to encoding 3 which is Basic RAPPOR with string categories.
  // That should fail.
  DoEncodeDoubleTest(expect_ok, value, 7, 3, expect_utc,
                     ObservationPart::kBasicRappor);

  // Now we switch to encoding 4 which is Basic RAPPOR with int categories.
  // That should fail.
  DoEncodeDoubleTest(expect_ok, value, 7, 4, expect_utc,
                     ObservationPart::kBasicRappor);
}

// Tests EncodeInt() with NoOp encoding as the specified encoding.
TEST(EncoderTest, EncodeIntNoOp) {
  // Metric 2 has a single integer part.
  // EncodingConfig 7 is NoOp
  auto obs = DoEncodeIntTest(42, 2, 7, true, ObservationPart::kUnencoded);
  EXPECT_EQ(42u,
            obs.parts().at("Part1").unencoded().unencoded_value().int_value());
}

// Tests EncodeBlob() with Forculus as the specified encoding.
TEST(EncoderTest, EncodeBlobForculus) {
  // Metric 3 has a single blob part.
  // EncodingConfig 1 is Forculus.
  std::string a_blob("This is a blob");
  DoEncodeBlobTest((const void*)a_blob.data(), a_blob.size(), 3, 1, false,
                   ObservationPart::kForculus);
}

// Tests EncodeBlob() with NoOp encoding as the specified encoding.
TEST(EncoderTest, EncodeBlobNoOp) {
  // Metric 3 has a single blob part.
  // EncodingConfig 7 is NoOp.
  std::string a_blob("This is a blob");
  auto obs = DoEncodeBlobTest((const void*)a_blob.data(), a_blob.size(), 3, 7,
                              false, ObservationPart::kUnencoded);
  EXPECT_EQ("This is a blob",
            obs.parts().at("Part1").unencoded().unencoded_value().blob_value());
}

// Tests EncodeIntBucketDistribution() with NoOp encoding.
TEST(EncoderTest, EncodeIntBucketDistributionNoOp) {
  // Metric 9 has a single int bucket distribution part.
  // EncodingConfig 7 is NoOp.
  std::map<uint32_t, uint64_t> distribution = {{0, 10}, {2, 6}, {11, 1}};
  bool expect_ok = true;
  bool expect_utc = true;
  auto obs = DoEncodeIntBucketDistributionTest(
      expect_ok, distribution, 8, 7, expect_utc, ObservationPart::kUnencoded);

  EXPECT_EQ(uint64_t(3), obs.parts()
                             .at("Part1")
                             .unencoded()
                             .unencoded_value()
                             .int_bucket_distribution()
                             .counts()
                             .size());

  for (auto it = distribution.begin(); it != distribution.end(); it++) {
    EXPECT_EQ(it->second, obs.parts()
                              .at("Part1")
                              .unencoded()
                              .unencoded_value()
                              .int_bucket_distribution()
                              .counts()
                              .at(it->first));
  }

  expect_ok = false;
  // Metric 1 has a single string part. That should fail.
  DoEncodeIntBucketDistributionTest(expect_ok, distribution, 1, 7, expect_utc,
                                    ObservationPart::kUnencoded);

  // Metric 2 has an integer part, but no int_buckets set. That should fail.
  DoEncodeIntBucketDistributionTest(expect_ok, distribution, 2, 7, expect_utc,
                                    ObservationPart::kUnencoded);

  // There are only 10 buckets + the overflow buckets configured.
  // This should fail.
  distribution[12] = 10;
  DoEncodeIntBucketDistributionTest(expect_ok, distribution, 8, 7, expect_utc,
                                    ObservationPart::kUnencoded);
}

// Tests the advanced API, when used corretly.
TEST(EncoderTest, AdvancedApiNoErrors) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();
  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret());

  Observation observation;
  Encoder::Value value;

  // EncodingConfig 2 is String RAPPOR
  value.AddStringPart(2, "city", "San Francisco");
  // EncodingConfig 4 is Basic RAPPOR with integer categories.
  value.AddIntPart(4, "rating", 125);
  // Metric 4 has a "city" part of type STRING and
  // a "rating" part of type INT.
  Encoder::Result result = encoder.Encode(4, value);

  // Check the result
  ASSERT_EQ(Encoder::kOK, result.status);
  ASSERT_NE(nullptr, result.observation);
  ASSERT_NE(nullptr, result.metadata);
  EXPECT_EQ(kCustomerId, result.metadata->customer_id());
  EXPECT_EQ(kProjectId, result.metadata->project_id());
  EXPECT_EQ(4u, result.metadata->metric_id());
  // We did not set the current time to a static value but rather used the
  // real time that the test was run. Sanity test the day index: It should
  // be at least the day on which this test was written and less than
  // 20 years in the future from that.
  EXPECT_TRUE(result.metadata->day_index() >= kPacificDayIndex);
  EXPECT_TRUE(result.metadata->day_index() < kPacificDayIndex + 365 * 20);

  EXPECT_NE("", result.observation->parts().at("city").rappor().data());
  EXPECT_NE("", result.observation->parts().at("rating").basic_rappor().data());
}

// Tests the advanced API, when used incorretly.
TEST(EncoderTest, AdvancedApiWithErrors) {
  // Build the ProjectContext encapsulating our test config data.
  std::shared_ptr<ProjectContext> project = GetTestProject();
  // Construct the Encoder.
  Encoder encoder(project, ClientSecret::GenerateNewSecret());

  std::unique_ptr<Encoder::Value> value(new Encoder::Value());

  // EncodingConfig 2 is String RAPPOR
  value->AddStringPart(2, "city", "San Francisco");

  // There is no metric 99.
  EXPECT_EQ(Encoder::kInvalidArguments, encoder.Encode(99, *value).status);

  // Metric 4 has two parts but value has only one part.
  EXPECT_EQ(Encoder::kInvalidArguments, encoder.Encode(4, *value).status);

  // EncodingConfig 4 is Basic RAPPOR with integer categories.
  value->AddIntPart(4, "rating", 1234);
  value->AddIntPart(4, "dummy", 1234);

  // Metric 4 has two parts but value has three parts.
  EXPECT_EQ(Encoder::kInvalidArguments, encoder.Encode(4, *value).status);

  value.reset(new Encoder::Value());
  value->AddStringPart(2, "city", "San Francisco");
  // "rating" is spelled wrong
  value->AddIntPart(4, "ratingx", 1234);
  EXPECT_EQ(Encoder::kInvalidArguments, encoder.Encode(4, *value).status);

  value.reset(new Encoder::Value());
  value->AddStringPart(2, "city", "San Francisco");
  // "rating" has the wrong type
  value->AddStringPart(4, "rating", "1234");
  EXPECT_EQ(Encoder::kInvalidArguments, encoder.Encode(4, *value).status);

  value.reset(new Encoder::Value());
  value->AddStringPart(2, "city", "San Francisco");
  // There is no encoding_config 99.
  value->AddIntPart(99, "rating", 1234);
  EXPECT_EQ(Encoder::kInvalidArguments, encoder.Encode(4, *value).status);

  // Forculus does not accept integer values.
  value.reset(new Encoder::Value());
  value->AddIntPart(1, "Part1", 42);
  EXPECT_EQ(Encoder::kInvalidArguments, encoder.Encode(2, *value).status);

  // String RAPPOR does not accept integer values.
  value.reset(new Encoder::Value());
  value->AddIntPart(2, "Part1", 42);
  EXPECT_EQ(Encoder::kInvalidArguments, encoder.Encode(2, *value).status);

  // String RAPPOR does not accept blob values.
  value.reset(new Encoder::Value());
  value->AddBlobPart(2, "Part1", (const void*)"1234", 4);
  EXPECT_EQ(Encoder::kInvalidArguments, encoder.Encode(3, *value).status);

  // Basic RAPPOR does not accept blob values.
  value.reset(new Encoder::Value());
  value->AddBlobPart(3, "Part1", (const void*)"1234", 4);
  EXPECT_EQ(Encoder::kInvalidArguments, encoder.Encode(3, *value).status);

  // Basic RAPPOR requires the value to be one of the candidates.
  value.reset(new Encoder::Value());
  value->AddStringPart(3, "Part1", "San Francisco");
  EXPECT_EQ(Encoder::kInvalidArguments, encoder.Encode(1, *value).status);

  // EncodingConfig 5 is an invalid Forculus config.
  value.reset(new Encoder::Value());
  value->AddStringPart(5, "Part1", "dummy");
  EXPECT_EQ(Encoder::kInvalidConfig, encoder.Encode(1, *value).status);

  // EncodingConfig 6 is an invalid String RAPPOR config.
  value.reset(new Encoder::Value());
  value->AddStringPart(6, "Part1", "dummy");
  EXPECT_EQ(Encoder::kInvalidConfig, encoder.Encode(1, *value).status);

  // Metric 5 is missing a time_zone_policy.
  value.reset(new Encoder::Value());
  value->AddStringPart(1, "Part1", "dummy");
  EXPECT_EQ(Encoder::kInvalidConfig, encoder.Encode(5, *value).status);
}

}  // namespace encoder
}  // namespace cobalt

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  INIT_LOGGING(argv[0]);
  return RUN_ALL_TESTS();
}

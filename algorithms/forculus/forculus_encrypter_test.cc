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

#include "algorithms/forculus/forculus_encrypter.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace forculus {

using encoder::ClientSecret;
using util::CalendarDate;

void TestEncrypterValidation(uint32_t threshold, bool use_valid_token,
    ForculusEncrypter::Status expected_status,
    int caller_line_number) {
  // Make a ClientSecret once and statically store the token.
  static const std::string kClientSecretToken =
      ClientSecret::GenerateNewSecret().GetToken();

  std::string client_secret_token =
      (use_valid_token ? kClientSecretToken : "Invalid Token");

  // Make a config with the given threshold
  ForculusConfig config;
  config.set_threshold(threshold);

  // Construct the Encrypter.
  ForculusEncrypter encrypter(config, 0, "", ClientSecret::FromToken(
      client_secret_token));

  // Invoke Encrypt() and check the status.
  ForculusObservation obs;
  EXPECT_EQ(expected_status,
    encrypter.Encrypt("hello", CalendarDate(), &obs))
     << "Invoked from line number: " << caller_line_number;
}

// A macro to invoke TestEncrypterValidation and pass it the current line num.
#define TEST_ENCRYPTER_VALIDATION(threshold, use_valid_token, \
                                  expected_status) \
    (TestEncrypterValidation(threshold, use_valid_token, \
     expected_status, __LINE__))

// Tests ForculusEncrypter config and input validation.
TEST(ForculusEncrypterTest, Validation) {
  bool use_valid_token = true;

  // threshold = 1: kInvalidConfig
  TEST_ENCRYPTER_VALIDATION(1, use_valid_token,
      ForculusEncrypter::kInvalidConfig);

  // threshold = 1: kInvalidConfig
  TEST_ENCRYPTER_VALIDATION(1, use_valid_token,
      ForculusEncrypter::kInvalidConfig);

  // threshold = 2: kOK
  TEST_ENCRYPTER_VALIDATION(2, use_valid_token, ForculusEncrypter::kOK);

  // threshold = UINT32_MAX: kInvalidConfig
  TEST_ENCRYPTER_VALIDATION(UINT32_MAX, use_valid_token,
      ForculusEncrypter::kInvalidConfig);

  // threshold = 1000: kOK
  TEST_ENCRYPTER_VALIDATION(1000, use_valid_token,
      ForculusEncrypter::kOK);

  // invalid token: kInvalidConfig
  use_valid_token = false;
  TEST_ENCRYPTER_VALIDATION(1000, use_valid_token,
      ForculusEncrypter::kInvalidConfig);
}

// Constructs a ForculusEncrypter and invoke Encrypt().
ForculusObservation Encrypt(const std::string& plaintext, uint32_t threshold,
  uint32_t metric_id, std::string metric_part_name,
  const std::string& secret_token, const CalendarDate& calendar_date) {
  // Make a config with the given threshold
  ForculusConfig config;
  config.set_threshold(threshold);

  // Construct the Encrypter.
  ForculusEncrypter encrypter(config, metric_id, metric_part_name,
      ClientSecret::FromToken(secret_token));

  // Invoke Encrypt() and check the status.
  ForculusObservation obs;
  EXPECT_EQ(ForculusEncrypter::kOK,
      encrypter.Encrypt(plaintext, calendar_date, &obs));
  return obs;
}


// We test all that we can about the Encrypter without doing any decryption.
// See forculus_decrypter_test.cc for tests that involve decryption.
TEST(ForculusEncrypterTest, SanityTest) {
  static const std::string kToken1 =
      ClientSecret::GenerateNewSecret().GetToken();
  static const std::string kToken2 =
      ClientSecret::GenerateNewSecret().GetToken();
  CalendarDate date1;
  date1.month = 1;
  date1.day_of_month = 1;
  date1.year = 2016;
  CalendarDate date2;
  date2.month = 1;
  date2.day_of_month = 2;
  date2.year = 2016;

  // The encryption and points should be deterministic as a function of
  // the inputs.
  ForculusObservation obs1 =
      Encrypt("Message 1", 20, 1, "part1", kToken1, date1);
  ForculusObservation obs2 =
      Encrypt("Message 1", 20, 1, "part1", kToken1, date1);
  EXPECT_EQ(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_EQ(obs1.point_x(), obs2.point_x());
  EXPECT_EQ(obs1.point_y(), obs2.point_y());

  // Different epochs should yield different ciphertexts and points.
  obs1 = Encrypt("Message 1", 20, 1, "part1", kToken1, date1);
  obs2 = Encrypt("Message 1", 20, 1, "part1", kToken1, date2);
  EXPECT_NE(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());

  // Different tokens should yield the same ciphertexts but different points.
  // This represents different clients doing the same threshold encryption.
  obs1 = Encrypt("Message 1", 20, 1, "part1", kToken1, date1);
  obs2 = Encrypt("Message 1", 20, 1, "part1", kToken2, date1);
  EXPECT_EQ(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());

  // Different metric parts should yield different ciphertexts and points.
  obs1 = Encrypt("Message 1", 20, 1, "part1", kToken1, date1);
  obs2 = Encrypt("Message 1", 20, 1, "part2", kToken1, date1);
  EXPECT_NE(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());

  // Different metric_ids should yield different ciphertexts and points.
  obs1 = Encrypt("Message 1", 20, 1, "part1", kToken1, date1);
  obs2 = Encrypt("Message 1", 20, 2, "part1", kToken1, date1);
  EXPECT_NE(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());

  // Different thresholds should yield different ciphertexts and points.
  obs1 = Encrypt("Message 1", 20, 1, "part1", kToken1, date1);
  obs2 = Encrypt("Message 1", 21, 1, "part1", kToken1, date1);
  EXPECT_NE(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());

  // Different plaintexts should yield different ciphertexts and points.
  obs1 = Encrypt("Message 1", 20, 1, "part1", kToken1, date1);
  obs2 = Encrypt("Message 2", 20, 1, "part1", kToken1, date1);
  EXPECT_NE(obs1.ciphertext(), obs2.ciphertext());
  EXPECT_NE(obs1.point_x(), obs2.point_x());
  EXPECT_NE(obs1.point_y(), obs2.point_y());
}

}  // namespace forculus

}  // namespace cobalt


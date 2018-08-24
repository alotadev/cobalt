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

#include "algorithms/rappor/rappor_test_utils.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace rappor {

bool IsSet(const std::string& data, int bit_index) {
  uint32_t num_bytes = data.size();
  uint32_t byte_index = bit_index / 8;
  uint32_t bit_in_byte_index = bit_index % 8;
  return data[num_bytes - byte_index - 1] & (1 << bit_in_byte_index);
}

std::string DataToBinaryString(const std::string& data) {
  size_t num_bits = data.size() * 8;
  // Initialize output to a string of all zeroes.
  std::string output(num_bits, '0');
  size_t output_index = 0;
  for (int bit_index = num_bits - 1; bit_index >= 0; bit_index--) {
    if (IsSet(data, bit_index)) {
      output[output_index] = '1';
    }
    output_index++;
  }
  return output;
}

std::string BuildBinaryString(size_t num_bits,
                              const std::vector<uint16_t>& index_of_1s) {
  // Initialize output to a string of all zeroes.
  std::string output(num_bits, '0');
  for (auto bit_index : index_of_1s) {
    output[num_bits - bit_index - 1] = '1';
  }
  return output;
}

std::string BinaryStringToData(const std::string& binary_string) {
  size_t num_bits = binary_string.size();
  EXPECT_EQ(0u, num_bits % 8);
  size_t num_bytes = num_bits / 8;
  std::string result(num_bytes, static_cast<char>(0));
  size_t byte_index = 0;
  uint8_t bit_mask = 1 << 7;
  for (char c : binary_string) {
    if (c == '1') {
      result[byte_index] |= bit_mask;
    }
    bit_mask >>= 1;
    if (bit_mask == 0) {
      byte_index++;
      bit_mask = 1 << 7;
    }
  }
  return result;
}

std::string CategoryName(uint32_t index) {
  char buffer[13];
  snprintf(buffer, sizeof(buffer), "category%04u", index);
  return std::string(buffer, 12);
}

std::string BuildBitPatternString(int num_bits, int index, char index_char,
                                  char other_char) {
  return std::string(num_bits - 1 - index, other_char) + index_char +
         std::string(index, other_char);
}

std::string CandidateString(int i) {
  return std::string("candidate string") + std::to_string(i);
}

// Populates |candidate_list| with |num_candidates| candidates;
void PopulateRapporCandidateList(uint32_t num_candidates,
                                 RapporCandidateList* candidate_list) {
  candidate_list->Clear();
  for (size_t i = 0; i < num_candidates; i++) {
    candidate_list->add_candidates(CandidateString(i));
  }
}

// Makes a RapporConfig with the given data.
RapporConfig Config(uint32_t num_bloom_bits, uint32_t num_cohorts,
                    uint32_t num_hashes, double p, double q) {
  RapporConfig config;
  config.set_num_bloom_bits(num_bloom_bits);
  config.set_num_hashes(num_hashes);
  config.set_num_cohorts(num_cohorts);
  config.set_prob_0_becomes_1(p);
  config.set_prob_1_stays_1(q);
  return config;
}

// Given a string of "0"s and "1"s of length a multiple of 8, and a cohort,
// returns a RapporObservation for the given cohort whose data is equal to the
// bytes whose binary representation is given by the string.
RapporObservation RapporObservationFromString(
    uint32_t cohort, const std::string& binary_string) {
  RapporObservation obs;
  obs.set_cohort(cohort);
  obs.set_data(BinaryStringToData(binary_string));
  return obs;
}


}  // namespace rappor
}  // namespace cobalt

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

#include <utility>
#include <vector>
#include <memory>

#include "config/encoding_config.h"
#include "config/metric_config.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace config {

using google::protobuf::io::ColumnNumber;
using google::protobuf::io::ErrorCollector;

class TestErrorCollector : public ErrorCollector {
 public:
  virtual ~TestErrorCollector() {}

  void AddError(int line, ColumnNumber column, const std::string & message)
      override {
    line_numbers_.push_back(line);
  }

  void AddWarning(int line, ColumnNumber column, const std::string & message)
      override {
    line_numbers_.push_back(line);
  }

  const std::vector<int>& line_numbers() {
    return line_numbers_;
  }

  std::vector<int> line_numbers_;
};

// TODO(rudominer) The tests in this file find the text files to read using
// file paths that are expressed relative to the Cobalt source root directory.
// This technique works because when the tests are run via the Python script
// cobaltb.py then the current working directory is that root directory.
// This technique is fragile. We should instead pass the path to the root
// directory as a command-line argument into the test. Currently there is no
// Cobalt infrastructure for passing command-line arguments to unit tests.

// Tests EncodingRegistry::FromFile() when a bad file path is used.
TEST(EncodingRegistryFromFile, BadFilePath) {
  auto result = EncodingRegistry::FromFile("not a valid path", nullptr);
  EXPECT_EQ(kFileOpenError, result.second);
}

// Tests MetricRegistry::FromFile() when a bad file path is used.
TEST(MetricRegistryFromFile, BadFilePath) {
  auto result = MetricRegistry::FromFile("not a valid path", nullptr);
  EXPECT_EQ(kFileOpenError, result.second);
}

// Tests ReportRegistry::FromFile() when a bad file path is used.
TEST(ReportRegistryFromFile, BadFilePath) {
  auto result = ReportRegistry::FromFile("not a valid path", nullptr);
  EXPECT_EQ(kFileOpenError, result.second);
}

// Tests EncodingRegistry::FromFile() when a valid file path is used but the
// file is not a valid ASCII proto file.
TEST(EncodingRegistryFromFile, NotValidAsciiProtoFile) {
  TestErrorCollector collector;
  EXPECT_EQ(0, collector.line_numbers().size());
  auto result = EncodingRegistry::FromFile("config/config_test.cc", &collector);
  EXPECT_EQ(kParsingError, result.second);
  EXPECT_EQ(1, collector.line_numbers().size());
  EXPECT_EQ(0, collector.line_numbers()[0]);
}

// Tests MetricRegistry::FromFile() when a valid file path is used but the
// file is not a valid ASCII proto file.
TEST(MetricRegistryFromFile, NotValidAsciiProtoFile) {
  TestErrorCollector collector;
  EXPECT_EQ(0, collector.line_numbers().size());
  auto result = MetricRegistry::FromFile("config/config_test.cc", &collector);
  EXPECT_EQ(kParsingError, result.second);
  EXPECT_EQ(1, collector.line_numbers().size());
  EXPECT_EQ(0, collector.line_numbers()[0]);
}

// Tests ReportRegistry::FromFile() when a valid file path is used but the
// file is not a valid ASCII proto file.
TEST(ReportRegistryFromFile, NotValidAsciiProtoFile) {
  TestErrorCollector collector;
  EXPECT_EQ(0, collector.line_numbers().size());
  auto result = ReportRegistry::FromFile("config/config_test.cc", &collector);
  EXPECT_EQ(kParsingError, result.second);
  EXPECT_EQ(1, collector.line_numbers().size());
  EXPECT_EQ(0, collector.line_numbers()[0]);
}

// Tests EncodingRegistry::FromFile() when a valid ASCII proto file is
// read but there is a duplicate registration.
TEST(EncodingRegistryFromFile, DuplicateRegistration) {
  auto result = EncodingRegistry::FromFile(
      "config/test_files/registered_encodings_contains_duplicate.txt", nullptr);
  EXPECT_EQ(kDuplicateRegistration, result.second);
}

// Tests MetricRegistry::FromFile() when a valid ASCII proto file is
// read but there is a duplicate registration.
TEST(MetricRegistryFromFile, DuplicateRegistration) {
  auto result = MetricRegistry::FromFile(
      "config/test_files/registered_metrics_contains_duplicate.txt", nullptr);
  EXPECT_EQ(kDuplicateRegistration, result.second);
}

// Tests ReportRegistry::FromFile() when a valid ASCII proto file is
// read but there is a duplicate registration.
TEST(ReportRegistryFromFile, DuplicateRegistration) {
  auto result = ReportRegistry::FromFile(
      "config/test_files/registered_reports_contains_duplicate.txt", nullptr);
  EXPECT_EQ(kDuplicateRegistration, result.second);
}

// Tests EncodingRegistry::FromFile() on a fully valid file.
TEST(EncodingRegistryFromFile, ValidFile) {
  auto result = EncodingRegistry::FromFile(
      "config/test_files/registered_encodings_valid.txt", nullptr);
  EXPECT_EQ(kOK, result.second);
  auto& registry = result.first;
  EXPECT_EQ(4, registry->size());

  // (1, 1, 1) Should be Forculus 20
  auto* encoding_config = registry->Get(1, 1, 1);
  EXPECT_EQ(20, encoding_config->forculus().threshold());

  // (1, 1, 2) Should be RAPPOR
  encoding_config = registry->Get(1, 1, 2);
  EXPECT_FLOAT_EQ(0.8, encoding_config->rappor().prob_1_stays_1());

  // (1, 1, 3) Should be not present
  EXPECT_EQ(nullptr, registry->Get(1, 1, 3));

  // (2, 1, 1) Should be Basic RAPPOR
  encoding_config = registry->Get(2, 1, 1);
  EXPECT_EQ(3, encoding_config->basic_rappor().category_size());
}

// Tests MetricRegistry::FromFile() on a fully valid file.
TEST(MetricRegistryFromFile, ValidFile) {
  auto result = MetricRegistry::FromFile(
      "config/test_files/registered_metrics_valid.txt", nullptr);
  EXPECT_EQ(kOK, result.second);
  auto& registry = result.first;
  EXPECT_EQ(4, registry->size());

  // (1, 1, 1) Should have 2 parts
  auto* metric_config = registry->Get(1, 1, 1);
  EXPECT_EQ(2, metric_config->parts_size());

  // (1, 1, 2) Should be "Fuschsia Usage by Hour"
  metric_config = registry->Get(1, 1, 2);
  EXPECT_EQ("Fuschsia Usage by Hour", metric_config->name());

  // (1, 1, 3) Should be not present
  EXPECT_EQ(nullptr, registry->Get(1, 1, 3));
}

// Tests ReportRegistry::FromFile() on a fully valid file.
TEST(ReportRegistryFromFile, ValidFile) {
  auto result = ReportRegistry::FromFile(
      "config/test_files/registered_reports_valid.txt", nullptr);
  EXPECT_EQ(kOK, result.second);
  auto& registry = result.first;
  EXPECT_EQ(4, registry->size());

  // (1, 1, 1) should have 2 variables
  auto* report_config = registry->Get(1, 1, 1);
  EXPECT_EQ(2, report_config->variable_size());

  // (1, 1, 2) Should be "Fuschsia Usage by Hour"
  report_config = registry->Get(1, 1, 2);
  EXPECT_EQ("Fuschsia Usage by Hour", report_config->name());

  // (1, 1, 3) Should be not present
  EXPECT_EQ(nullptr, registry->Get(1, 1, 3));
}

// This test runs EncodingRegistry::FromFile() on our official registration
// file, registered_encodings.txt. The purpose is to validate that file.
TEST(EncodingRegistryFromFile, CheckRegisteredEncodings) {
  auto result = EncodingRegistry::FromFile(
      "config/registered/registered_encodings.txt", nullptr);
  EXPECT_EQ(kOK, result.second);
}

// This test runs MetricRegistry::FromFile() on our official registration
// file, registered_metrics.txt. The purpose is to validate that file.
TEST(MetricRegistryFromFile, CheckRegisteredMetrics) {
  auto result = MetricRegistry::FromFile(
      "config/registered/registered_metrics.txt", nullptr);
  EXPECT_EQ(kOK, result.second);
}

// This test runs ReportRegistry::FromFile() on our official registration
// file, registered_reports.txt. The purpose is to validate that file.
TEST(ReportRegistryFromFile, CheckRegisteredReports) {
  auto result = ReportRegistry::FromFile(
      "config/registered/registered_reports.txt", nullptr);
  EXPECT_EQ(kOK, result.second);
}

}  // namespace config
}  // namespace cobalt


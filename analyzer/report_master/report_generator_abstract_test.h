// Copyright 2017 The Fuchsia Authors
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

#ifndef COBALT_ANALYZER_REPORT_MASTER_REPORT_GENERATOR_ABSTRACT_TEST_H_
#define COBALT_ANALYZER_REPORT_MASTER_REPORT_GENERATOR_ABSTRACT_TEST_H_

#include "analyzer/report_master/report_generator.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "./observation.pb.h"
#include "analyzer/report_master/report_exporter.h"
#include "encoder/client_secret.h"
#include "encoder/encoder.h"
#include "encoder/project_context.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

// This file contains type-parameterized tests of ReportGenerator.
//
// We use C++ templates along with the macros TYPED_TEST_CASE_P and
// TYPED_TEST_P in order to define test templates that may be instantiated to
// to produce concrete tests that use various implementations of Datastore.
//
// See report_generator_test.cc and report_generator_emulator_test.cc for the
// concrete instantiations.
//
// NOTE: If you add a new test to this file you must add its name to the
// invocation REGISTER_TYPED_TEST_CASE_P macro at the bottom of this file.

namespace cobalt {
namespace analyzer {

namespace testing {

const uint32_t kCustomerId = 1;
const uint32_t kProjectId = 1;
const uint32_t kMetricId = 1;
const uint32_t kReportConfigId = 1;
const uint32_t kForculusEncodingConfigId = 1;
const uint32_t kBasicRapporEncodingConfigId = 2;
const char kPartName1[] = "Part1";
const char kPartName2[] = "Part2";
const size_t kForculusThreshold = 20;

// This unix timestamp corresponds to Friday Dec 2, 2016 in UTC
const time_t kSomeTimestamp = 1480647356;
// This is the day index for Friday Dec 2, 2016
const uint32_t kDayIndex = 17137;

const char* kMetricConfigText = R"(
# Metric 1 has two string parts.
element {
  customer_id: 1
  project_id: 1
  id: 1
  time_zone_policy: UTC
  parts {
    key: "Part1"
    value {
    }
  }
  parts {
    key: "Part2"
    value {
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

# EncodingConfig 2 is Basic RAPPOR.
element {
  customer_id: 1
  project_id: 1
  id: 2
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

)";

const char* kReportConfigText = R"(
# ReportConfig 1 specifies a report of both variables of Metric 1.
element {
  customer_id: 1
  project_id: 1
  id: 1
  metric_id: 1
  variable {
    metric_part: "Part1"
  }
  variable {
    metric_part: "Part2"
  }
  export_configs {
    csv {}
    gcs {
      bucket: "BUCKET-NAME"
      folder_path: "report_exporter_test/fruit_counts"
    }
  }
}

)";

// An implementation of GcsUploadInterface that saves its parameters and
// returns OK.
struct FakeGcsUploader : public GcsUploadInterface {
  grpc::Status UploadToGCS(const std::string& bucket, const std::string& path,
                           const std::string& mime_type,
                           const std::string& serialized_report) override {
    this->upload_was_invoked = true;
    this->bucket = bucket;
    this->path = path;
    this->mime_type = mime_type;
    this->serialized_report = serialized_report;
    return grpc::Status::OK;
  }

  bool upload_was_invoked = false;
  std::string bucket;
  std::string path;
  std::string mime_type;
  std::string serialized_report;
};

}  // namespace testing

// ReportGeneratorAbstractTest is templatized on the parameter
// |StoreFactoryClass| which must be the name of a class that contains the
// following method: static DataStore* NewStore()
// See MemoryStoreFactory in sotre/memory_store_test_helper.h and
// BigtableStoreEmulatorFactory in store/bigtable_emulator_helper.h.
template <class StoreFactoryClass>
class ReportGeneratorAbstractTest : public ::testing::Test {
 protected:
  ReportGeneratorAbstractTest()
      : data_store_(StoreFactoryClass::NewStore()),
        observation_store_(new store::ObservationStore(data_store_)),
        report_store_(new store::ReportStore(data_store_)),
        fake_uploader_(new testing::FakeGcsUploader()) {
    report_id_.set_customer_id(testing::kCustomerId);
    report_id_.set_project_id(testing::kProjectId);
    report_id_.set_report_config_id(testing::kReportConfigId);
  }

  void SetUp() {
    // Clear the DataStore.
    EXPECT_EQ(store::kOK,
              data_store_->DeleteAllRows(store::DataStore::kObservations));
    EXPECT_EQ(store::kOK,
              data_store_->DeleteAllRows(store::DataStore::kReportMetadata));
    EXPECT_EQ(store::kOK,
              data_store_->DeleteAllRows(store::DataStore::kReportRows));

    // Parse the metric config string
    auto metric_parse_result =
        config::MetricRegistry::FromString(testing::kMetricConfigText, nullptr);
    EXPECT_EQ(config::kOK, metric_parse_result.second);
    std::shared_ptr<config::MetricRegistry> metric_registry(
        metric_parse_result.first.release());

    // Parse the encoding config string
    auto encoding_parse_result = config::EncodingRegistry::FromString(
        testing::kEncodingConfigText, nullptr);
    EXPECT_EQ(config::kOK, encoding_parse_result.second);
    std::shared_ptr<config::EncodingRegistry> encoding_config_registry(
        encoding_parse_result.first.release());

    // Parse the report config string
    auto report_parse_result =
        config::ReportRegistry::FromString(testing::kReportConfigText, nullptr);
    EXPECT_EQ(config::kOK, report_parse_result.second);
    std::shared_ptr<config::ReportRegistry> report_config_registry(
        report_parse_result.first.release());

    // Make a ProjectContext
    project_.reset(
        new encoder::ProjectContext(testing::kCustomerId, testing::kProjectId,
                                    metric_registry, encoding_config_registry));

    std::shared_ptr<config::AnalyzerConfig> analyzer_config(
        new config::AnalyzerConfig(encoding_config_registry, metric_registry,
                                   report_config_registry));

    // Make the ReportGenerator
    std::unique_ptr<ReportExporter> report_exporter(
        new ReportExporter(fake_uploader_));
    report_generator_.reset(
        new ReportGenerator(analyzer_config, observation_store_, report_store_,
                            std::move(report_exporter)));
  }

  // Makes an Observation with two string parts, both of which have the
  // given |string_value|, using the encoding with the given encoding_config_id.
  std::unique_ptr<Observation> MakeObservation(std::string string_value,
                                               uint32_t encoding_config_id) {
    // Construct a new Encoder with a new client secret.
    encoder::Encoder encoder(project_,
                             encoder::ClientSecret::GenerateNewSecret());
    // Set a static current time so we know we have a static day_index.
    encoder.set_current_time(testing::kSomeTimestamp);

    // Construct the two-part value to add.
    encoder::Encoder::Value value;
    value.AddStringPart(encoding_config_id, testing::kPartName1, string_value);
    value.AddStringPart(encoding_config_id, testing::kPartName2, string_value);

    // Encode an observation.
    encoder::Encoder::Result result = encoder.Encode(testing::kMetricId, value);
    EXPECT_EQ(encoder::Encoder::kOK, result.status);
    EXPECT_TRUE(result.observation.get() != nullptr);
    EXPECT_EQ(2, result.observation->parts_size());
    return std::move(result.observation);
  }

  // Adds to the ObservationStore |num_clients| observations of our test metric
  // that each encode the given string |value| using the given
  // |encoding_config_id|. Each Observation is generated as if from a different
  // client.
  void AddObservations(std::string value, uint32_t encoding_config_id,
                       int num_clients) {
    std::vector<Observation> observations;
    for (int i = 0; i < num_clients; i++) {
      observations.emplace_back(*MakeObservation(value, encoding_config_id));
    }
    ObservationMetadata metadata;
    metadata.set_customer_id(testing::kCustomerId);
    metadata.set_project_id(testing::kProjectId);
    metadata.set_metric_id(testing::kMetricId);
    metadata.set_day_index(testing::kDayIndex);
    EXPECT_EQ(store::kOK,
              observation_store_->AddObservationBatch(metadata, observations));
  }

  struct GeneratedReport {
    ReportMetadataLite metadata;
    ReportRows rows;
  };

  // Uses the ReportGenerator to generate a HISTOGRAM report that analyzes the
  // specified variable of our two-variable test metric. |variable_index| must
  // be either 0 or 1. It will also be used for the sequence_num.
  // If |export_report| is true then the report will be exported using
  // our FakeGcsUploader.
  GeneratedReport GenerateHistogramReport(int variable_index,
                                          bool export_report) {
    // Complete the report_id by specifying the sequence_num.
    report_id_.set_sequence_num(variable_index);

    // Start a report for the specified variable, for the interval of days
    // [kDayIndex, kDayIndex].
    std::string export_name = export_report ? "export_name" : "";
    EXPECT_EQ(store::kOK,
              report_store_->StartNewReport(
                  testing::kDayIndex, testing::kDayIndex, true, export_name,
                  HISTOGRAM, {(uint32_t)variable_index}, &report_id_));

    // Generate the report
    EXPECT_TRUE(report_generator_->GenerateReport(report_id_).ok());

    // Fetch the report from the ReportStore.
    GeneratedReport report;
    EXPECT_EQ(store::kOK, report_store_->GetReport(report_id_, &report.metadata,
                                                   &report.rows));

    return report;
  }

  // Adds to the ObservationStore a bunch of Observations of our test metric
  // that use our test Forculus encoding config in which the Forculus threshold
  // is 20. Each Observation is generated as if from a different client.
  // We simulate 20 clients adding "hello", 19 clients adding "goodbye", and
  // 21 clients adding "peace". Thus we expect "hello" and "peace" to appear
  // in the generated report but not "goodybe".
  void AddForculusObservations() {
    // Add 20 copies of the Observation "hello"
    AddObservations("hello", testing::kForculusEncodingConfigId,
                    testing::kForculusThreshold);

    // Add 19 copies of the Observation "goodbye"
    AddObservations("goodbye", testing::kForculusEncodingConfigId,
                    testing::kForculusThreshold - 1);

    // Add 21 copies of the Observation "peace"
    AddObservations("peace", testing::kForculusEncodingConfigId,
                    testing::kForculusThreshold + 1);
  }

  // This is the CSV that should be generated when the report for metric part 2
  // is exported, when Forculus Observations are added, based on the
  // Observations that are added in AddForculusObservations() above.
  const char* const kExpectedPart2ForculusCSV = R"(date,Part2,count,err
2016-12-2,"hello",20.000,0
2016-12-2,"peace",21.000,0
)";

  // This method should be invoked after invoking AddForculusObservations()
  // and then GenerateReport. It checks the generated Report to make sure
  // it is correct given the Observations that were added and the Forculus
  // config.
  void CheckForculusReport(const GeneratedReport& report, uint variable_index,
                           const std::string& expected_export_csv) {
    EXPECT_EQ(HISTOGRAM, report.metadata.report_type());
    EXPECT_EQ(1, report.metadata.variable_indices_size());
    EXPECT_EQ(variable_index, report.metadata.variable_indices(0));
    EXPECT_EQ(2, report.rows.rows_size());
    for (const auto& report_row : report.rows.rows()) {
      EXPECT_EQ(0, report_row.histogram().std_error());
      ValuePart recovered_value;
      EXPECT_TRUE(report_row.histogram().has_value());
      recovered_value = report_row.histogram().value();

      EXPECT_EQ(ValuePart::kStringValue, recovered_value.data_case());
      std::string string_value = recovered_value.string_value();
      int count_estimate = report_row.histogram().count_estimate();
      switch (count_estimate) {
        case 20:
          EXPECT_EQ("hello", string_value);
          break;
        case 21:
          EXPECT_EQ("peace", string_value);
          break;
        default:
          FAIL();
      }
    }
    if (report.metadata.export_name() == "") {
      EXPECT_FALSE(this->fake_uploader_->upload_was_invoked);
    } else {
      EXPECT_TRUE(this->fake_uploader_->upload_was_invoked);
      // Reset for next time
      this->fake_uploader_->upload_was_invoked = false;
      EXPECT_EQ("BUCKET-NAME", fake_uploader_->bucket);
      EXPECT_EQ("report_exporter_test/fruit_counts/export_name.csv",
                fake_uploader_->path);
      EXPECT_EQ("text/csv", fake_uploader_->mime_type);
      EXPECT_EQ(expected_export_csv, fake_uploader_->serialized_report);
    }
  }

  // Adds to the ObservationStore a bunch of Observations of our test metric
  // that use our test BasicRappor encoding config. We add 100 observations of
  // "Apple", 200 observations of "Banana", and 300 observations of
  // "Cantaloupe".
  void AddBasicRapporObservations() {
    AddObservations("Apple", testing::kBasicRapporEncodingConfigId, 100);
    AddObservations("Banana", testing::kBasicRapporEncodingConfigId, 200);
    AddObservations("Cantaloupe", testing::kBasicRapporEncodingConfigId, 300);
  }

  // This method should be invoked after invoking AddBasicRapporObservations()
  // and then GenerateReport. It checks the generated Report to make sure
  // it is correct given the Observations that were added. We are not attempting
  // to validate the Basic RAPPOR algorithm here so we simply test that the
  // all three strings appear with a non-zero count and under the correct
  // variable index.
  void CheckBasicRapporReport(const GeneratedReport& report,
                              uint variable_index) {
    EXPECT_EQ(HISTOGRAM, report.metadata.report_type());
    EXPECT_EQ(1, report.metadata.variable_indices_size());
    EXPECT_EQ(variable_index, report.metadata.variable_indices(0));
    EXPECT_EQ(3, report.rows.rows_size());
    for (const auto& report_row : report.rows.rows()) {
      EXPECT_NE(0, report_row.histogram().std_error());
      ValuePart recovered_value;
      EXPECT_TRUE(report_row.histogram().has_value());
      recovered_value = report_row.histogram().value();
      break;

      EXPECT_EQ(ValuePart::kStringValue, recovered_value.data_case());
      std::string string_value = recovered_value.string_value();
      EXPECT_TRUE(string_value == "Apple" || string_value == "Banana" ||
                  string_value == "Cantaloupe");

      EXPECT_GT(report_row.histogram().count_estimate(), 0);
    }
    if (report.metadata.export_name() == "") {
      EXPECT_FALSE(this->fake_uploader_->upload_was_invoked);
    } else {
      EXPECT_TRUE(this->fake_uploader_->upload_was_invoked);
      // Reset for next time
      this->fake_uploader_->upload_was_invoked = false;
      EXPECT_EQ("BUCKET-NAME", fake_uploader_->bucket);
      EXPECT_EQ("report_exporter_test/fruit_counts/export_name.csv",
                fake_uploader_->path);
      EXPECT_EQ("text/csv", fake_uploader_->mime_type);
      EXPECT_FALSE(fake_uploader_->serialized_report.empty());
    }
  }

  ReportId report_id_;
  std::shared_ptr<encoder::ProjectContext> project_;
  std::shared_ptr<store::DataStore> data_store_;
  std::shared_ptr<store::ObservationStore> observation_store_;
  std::shared_ptr<store::ReportStore> report_store_;
  std::unique_ptr<ReportGenerator> report_generator_;
  std::shared_ptr<testing::FakeGcsUploader> fake_uploader_;
};

TYPED_TEST_CASE_P(ReportGeneratorAbstractTest);

// Tests that the ReportGenerator correctly generates a report for both
// variables of our two-variable metric when the ObservationStore has been
// filled with Observations of that metric that use our Forculus encoding.
// Note that *joint* reports have not yet been implemented.
TYPED_TEST_P(ReportGeneratorAbstractTest, Forculus) {
  this->AddForculusObservations();
  {
    SCOPED_TRACE("variable_index = 0");
    int variable_index = 0;
    // Don't export the report.
    auto report = this->GenerateHistogramReport(variable_index, false);
    this->CheckForculusReport(report, variable_index, "");
  }
  {
    SCOPED_TRACE("variable_index = 1");
    int variable_index = 1;
    // Do export the report.
    auto report = this->GenerateHistogramReport(variable_index, true);
    this->CheckForculusReport(report, variable_index,
                              this->kExpectedPart2ForculusCSV);
  }
}

// Tests that the ReportGenerator correctly generates a report for both
// variables of our two-variable metric when the ObservationStroe has been
// filled with Observations of that metric that use our Basic RAPPOR encoding.
// Note that *joint* reports have not yet been implemented.
TYPED_TEST_P(ReportGeneratorAbstractTest, BasicRappor) {
  this->AddBasicRapporObservations();
  {
    SCOPED_TRACE("variable_index = 0");
    int variable_index = 0;
    // Do exort the report.
    auto report = this->GenerateHistogramReport(variable_index, true);
    this->CheckBasicRapporReport(report, variable_index);
  }
  {
    SCOPED_TRACE("variable_index = 1");
    int variable_index = 1;
    // Don't export the report.
    auto report = this->GenerateHistogramReport(variable_index, false);
    this->CheckBasicRapporReport(report, variable_index);
  }
}

REGISTER_TYPED_TEST_CASE_P(ReportGeneratorAbstractTest, Forculus, BasicRappor);

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_REPORT_MASTER_REPORT_GENERATOR_ABSTRACT_TEST_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/logger/logger.h"

#include <memory>
#include <string>

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>

#include "src/algorithms/rappor/rappor_encoder.h"
#include "src/lib/util/clock.h"
#include "src/lib/util/datetime_util.h"
#include "src/lib/util/encrypted_message_util.h"
#include "src/lib/util/testing/test_with_files.h"
#include "src/local_aggregation/event_aggregator_mgr.h"
#include "src/local_aggregation/test_utils/test_event_aggregator_mgr.h"
#include "src/logger/encoder.h"
#include "src/logger/fake_logger.h"
#include "src/logger/logger_test_utils.h"
#include "src/logger/project_context.h"
#include "src/logger/status.h"
#include "src/logger/testing_constants.h"
#include "src/pb/observation.pb.h"
#include "src/pb/observation2.pb.h"
#include "src/registry/packed_event_codes.h"
#include "src/system_data/client_secret.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

#ifndef PROTO_LITE
using ::google::protobuf::util::MessageDifferencer;
#endif

namespace cobalt::logger {

using local_aggregation::EventAggregatorManager;
using local_aggregation::TestEventAggregatorManager;
using system_data::ClientSecret;
using system_data::SystemDataInterface;
using testing::CheckNumericEventObservations;
using testing::ExpectedAggregationParams;
using testing::ExpectedPerDeviceNumericObservations;
using testing::ExpectedReportParticipationObservations;
using testing::ExpectedUniqueActivesObservations;
using testing::FakeObservationStore;
using testing::FetchObservations;
using testing::FetchSingleObservation;
using testing::GetTestProject;
using testing::TestUpdateRecipient;
using util::EncryptedMessageMaker;
using util::FakeValidatedClock;
using util::IncrementingSystemClock;
using util::TimeToDayIndex;

namespace {
// Number of seconds in a day
constexpr int kDay = 60 * 60 * 24;
// Number of seconds in an ideal year
constexpr int kYear = kDay * 365;

}  // namespace

class LoggerTest : public util::testing::TestWithFiles {
 protected:
  void SetUp() override {
    SetUpFromMetrics(testing::all_report_types::kCobaltRegistryBase64,
                     testing::all_report_types::kExpectedAggregationParams);
  }

  // Set up LoggerTest using a base64-encoded registry.
  void SetUpFromMetrics(const std::string& registry_base64,
                        const ExpectedAggregationParams& expected_aggregation_params) {
    MakeTestFolder();
    expected_aggregation_params_ = expected_aggregation_params;
    observation_store_ = std::make_unique<FakeObservationStore>();
    update_recipient_ = std::make_unique<TestUpdateRecipient>();
    observation_encrypter_ = EncryptedMessageMaker::MakeUnencrypted();
    observation_writer_ = std::make_unique<ObservationWriter>(
        observation_store_.get(), update_recipient_.get(), observation_encrypter_.get());
    encoder_ = std::make_unique<Encoder>(ClientSecret::GenerateNewSecret(), system_data_.get());

    CobaltConfig cfg = {.client_secret = system_data::ClientSecret::GenerateNewSecret()};

    cfg.local_aggregation_backfill_days = 0;
    cfg.local_aggregate_proto_store_path = aggregate_store_path();
    cfg.obs_history_proto_store_path = obs_history_path();

    event_aggregator_mgr_ = std::make_unique<TestEventAggregatorManager>(cfg, fs(), encoder_.get(),
                                                                         observation_writer_.get());
    internal_logger_ = std::make_unique<testing::FakeLogger>();

    // Create a mock clock which does not increment by default when called.
    // Set the time to 1 year after the start of Unix time so that the start
    // date of any aggregation window falls after the start of time.
    mock_clock_ = std::make_unique<IncrementingSystemClock>(std::chrono::system_clock::duration(0));
    mock_clock_->set_time(std::chrono::system_clock::time_point(std::chrono::seconds(kYear)));
    validated_clock_ = std::make_unique<FakeValidatedClock>(mock_clock_.get());
    validated_clock_->SetAccurate(true);

    undated_event_manager_ = std::make_shared<UndatedEventManager>(
        encoder_.get(), event_aggregator_mgr_->GetEventAggregator(), observation_writer_.get(),
        system_data_.get());

    logger_ = std::make_unique<Logger>(
        GetTestProject(registry_base64), encoder_.get(),
        event_aggregator_mgr_->GetEventAggregator(), observation_writer_.get(), system_data_.get(),
        validated_clock_.get(), undated_event_manager_, internal_logger_.get());
  }

  void TearDown() override {
    event_aggregator_mgr_.reset();
    logger_.reset();
  }

  // Returns the day index of the current day according to |mock_clock_|, in
  // |time_zone|, without incrementing the clock.
  uint32_t CurrentDayIndex(MetricDefinition::TimeZonePolicy time_zone) {
    return TimeToDayIndex(std::chrono::system_clock::to_time_t(mock_clock_->peek_now()), time_zone);
  }

  // Clears the FakeObservationStore and resets counts of Observations received
  // by the FakeObservationStore and the TestUpdateRecipient.
  void ResetObservationStore() {
    observation_store_->messages_received.clear();
    observation_store_->metadata_received.clear();
    observation_store_->ResetObservationCounter();
    update_recipient_->invocation_count = 0;
  }

  std::unique_ptr<Logger> logger_;
  std::unique_ptr<testing::FakeLogger> internal_logger_;
  std::unique_ptr<ObservationWriter> observation_writer_;
  std::unique_ptr<FakeObservationStore> observation_store_;
  std::unique_ptr<FakeValidatedClock> validated_clock_;
  std::shared_ptr<UndatedEventManager> undated_event_manager_;
  std::unique_ptr<TestUpdateRecipient> update_recipient_;
  ExpectedAggregationParams expected_aggregation_params_;

  std::unique_ptr<Encoder> encoder_;
  std::unique_ptr<TestEventAggregatorManager> event_aggregator_mgr_;
  std::unique_ptr<EncryptedMessageMaker> observation_encrypter_;
  std::unique_ptr<SystemDataInterface> system_data_;
  std::unique_ptr<IncrementingSystemClock> mock_clock_;
};

// Creates a Logger for a ProjectContext where each of the metrics
// has a report of type PER_DEVICE_NUMERIC_STATS.
class PerDeviceNumericLoggerTest : public LoggerTest {
 protected:
  void SetUp() override {
    SetUpFromMetrics(testing::per_device_numeric_stats::kCobaltRegistryBase64,
                     testing::per_device_numeric_stats::kExpectedAggregationParams);
  }
};

// Tests the method LogEvent().
TEST_F(LoggerTest, LogEvent) {
  ASSERT_EQ(kOK, logger_->LogEvent(testing::all_report_types::kErrorOccurredMetricId, 42));
  Observation2 observation;
  uint32_t expected_report_id = testing::all_report_types::kErrorOccurredErrorCountsByCodeReportId;
  ASSERT_TRUE(FetchSingleObservation(&observation, expected_report_id, observation_store_.get(),
                                     update_recipient_.get()));
  ASSERT_TRUE(observation.has_basic_rappor());
  EXPECT_FALSE(observation.basic_rappor().data().empty());
}

// Tests the method LogEventCount().
TEST_F(LoggerTest, LogEventCount) {
  std::vector<uint32_t> expected_report_ids = {
      testing::all_report_types::kReadCacheHitsReadCacheHitCountsReportId,
      testing::all_report_types::kReadCacheHitsReadCacheHitHistogramsReportId,
      testing::all_report_types::kReadCacheHitsReadCacheHitStatsReportId};

  ASSERT_EQ(kOK, logger_->LogEventCount(testing::all_report_types::kReadCacheHitsMetricId, 43,
                                        "component2", 1, 303));
  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 43u, "component2", 303,
                                            observation_store_.get(), update_recipient_.get()));
}

// Tests the method LogEventCount() can accept large numbers correctly.
TEST_F(LoggerTest, LogEventCountWithLargeCounts) {
  std::vector<uint32_t> expected_report_ids = {
      testing::all_report_types::kReadCacheHitsReadCacheHitCountsReportId,
      testing::all_report_types::kReadCacheHitsReadCacheHitHistogramsReportId,
      testing::all_report_types::kReadCacheHitsReadCacheHitStatsReportId};

  int64_t large_value = 3'147'483'647;
  ASSERT_EQ(kOK, logger_->LogEventCount(testing::all_report_types::kReadCacheHitsMetricId, 43,
                                        "component2", 1, large_value));
  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 43u, "component2", large_value,
                                            observation_store_.get(), update_recipient_.get()));
}

// Tests the version of the method LogEventCount() that accepts a vector of
// event codes.
TEST_F(LoggerTest, LogEventCountMultiDimension) {
  // Use no event codes when the metric has one dimension. Expect kOk.
  EXPECT_EQ(kOK, logger_->LogEventCount(testing::all_report_types::kReadCacheHitsMetricId,
                                        std::vector<uint32_t>({}), "", 0, 303));

  std::vector<uint32_t> expected_report_ids = {
      testing::all_report_types::kReadCacheHitsReadCacheHitCountsReportId,
      testing::all_report_types::kReadCacheHitsReadCacheHitHistogramsReportId,
      testing::all_report_types::kReadCacheHitsReadCacheHitStatsReportId};
  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 0, "", 303,
                                            observation_store_.get(), update_recipient_.get()));
  ResetObservationStore();

  // Use two event codes when the metric has one dimension. Expect
  // kInvalidArguments.
  EXPECT_EQ(kInvalidArguments,
            logger_->LogEventCount(testing::all_report_types::kReadCacheHitsMetricId,
                                   std::vector<uint32_t>({43, 44}), "", 0, 303));

  // All good, expect OK.
  EXPECT_EQ(kOK, logger_->LogEventCount(testing::all_report_types::kReadCacheHitsMetricId,
                                        std::vector<uint32_t>({43}), "", 0, 303));

  expected_report_ids = {testing::all_report_types::kReadCacheHitsReadCacheHitCountsReportId,
                         testing::all_report_types::kReadCacheHitsReadCacheHitHistogramsReportId,
                         testing::all_report_types::kReadCacheHitsReadCacheHitStatsReportId};
  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 43u, "", 303,
                                            observation_store_.get(), update_recipient_.get()));
}

// Tests the method LogElapsedTime().
TEST_F(LoggerTest, LogElapsedTime) {
  std::vector<uint32_t> expected_report_ids = {
      testing::all_report_types::kModuleLoadTimeModuleLoadTimeAggregatedReportId,
      testing::all_report_types::kModuleLoadTimeModuleLoadTimeHistogramReportId,
      testing::all_report_types::kModuleLoadTimeModuleLoadTimeRawDumpReportId};

  // Use a zero event code when the metric does not have any metric dimensions
  // set. This is OK by convention. The zero will be ignored.
  ASSERT_EQ(kOK, logger_->LogElapsedTime(testing::all_report_types::kModuleLoadTimeMetricId, 0,
                                         "component4", 4004));

  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 0u, "component4", 4004,
                                            observation_store_.get(), update_recipient_.get()));
}

// Tests the version of the method LogElapsedTime() that accepts a vector of
// event codes.
TEST_F(LoggerTest, LogElapsedTimeMultiDimension) {
  std::vector<uint32_t> expected_report_ids = {
      testing::all_report_types::kModuleLoadTimeModuleLoadTimeAggregatedReportId,
      testing::all_report_types::kModuleLoadTimeModuleLoadTimeHistogramReportId,
      testing::all_report_types::kModuleLoadTimeModuleLoadTimeRawDumpReportId};

  // Use a non-zero event code even though the metric does not have any
  // metric dimensions defined. Expect kInvalidArgument.
  ASSERT_EQ(kInvalidArguments,
            logger_->LogElapsedTime(testing::all_report_types::kModuleLoadTimeMetricId,
                                    std::vector<uint32_t>({44}), "component4", 4004));

  // Use a vector of length two even though the metric does not have any
  // metric dimensions defined. Expect kInvalidArgument.
  ASSERT_EQ(kInvalidArguments,
            logger_->LogElapsedTime(testing::all_report_types::kModuleLoadTimeMetricId,
                                    std::vector<uint32_t>({0, 0}), "component4", 4004));

  // Use an empty vector of event codes when the metric does not have any metric
  // dimensions set. This is good.
  ASSERT_EQ(kOK, logger_->LogElapsedTime(testing::all_report_types::kModuleLoadTimeMetricId,
                                         std::vector<uint32_t>({}), "component4", 4004));

  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 0u, "component4", 4004,
                                            observation_store_.get(), update_recipient_.get()));
}

// Tests the method LogFrameRate().
TEST_F(LoggerTest, LogFrameRate) {
  std::vector<uint32_t> expected_report_ids = {
      testing::all_report_types::kLoginModuleFrameRateLoginModuleFrameRateAggregatedReportId,
      testing::all_report_types::kLoginModuleFrameRateLoginModuleFrameRateHistogramReportId,
      testing::all_report_types::kLoginModuleFrameRateLoginModuleFrameRateRawDumpReportId};
  ASSERT_EQ(kOK, logger_->LogFrameRate(testing::all_report_types::kLoginModuleFrameRateMetricId, 45,
                                       "component5", 5.123));
  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 45u, "component5", 5123,
                                            observation_store_.get(), update_recipient_.get()));
}

// Tests the version of the method LogFrameRate() that accepts a vector of
// event codes.
TEST_F(LoggerTest, LogFrameRateMultiDimension) {
  std::vector<uint32_t> expected_report_ids = {
      testing::all_report_types::kLoginModuleFrameRateLoginModuleFrameRateAggregatedReportId,
      testing::all_report_types::kLoginModuleFrameRateLoginModuleFrameRateHistogramReportId,
      testing::all_report_types::kLoginModuleFrameRateLoginModuleFrameRateRawDumpReportId};

  // Use no event codes when the metric has one dimension. Expect kOK.
  ASSERT_EQ(kOK, logger_->LogFrameRate(testing::all_report_types::kLoginModuleFrameRateMetricId,
                                       std::vector<uint32_t>({}), "", 5.123));
  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 0u, "", 5123,
                                            observation_store_.get(), update_recipient_.get()));
  ResetObservationStore();

  // Use two event codes when the metric has one dimension. Expect
  // kInvalidArguments.
  ASSERT_EQ(kInvalidArguments,
            logger_->LogFrameRate(testing::all_report_types::kLoginModuleFrameRateMetricId,
                                  std::vector<uint32_t>({45, 46}), "", 5.123));

  // All good
  ASSERT_EQ(kOK, logger_->LogFrameRate(testing::all_report_types::kLoginModuleFrameRateMetricId,
                                       std::vector<uint32_t>({45}), "", 5.123));
  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 45u, "", 5123,
                                            observation_store_.get(), update_recipient_.get()));
}

// Tests the method LogMemoryUsage().
TEST_F(LoggerTest, LogMemoryUsage) {
  std::vector<uint32_t> expected_report_ids = {
      testing::all_report_types::kLedgerMemoryUsageLedgerMemoryUsageAggregatedReportId,
      testing::all_report_types::kLedgerMemoryUsageLedgerMemoryUsageHistogramReportId};

  // The simple version of LogMemoryUsage() works, even though the metric has 2 dimensions.
  ASSERT_EQ(kOK, logger_->LogMemoryUsage(testing::all_report_types::kLedgerMemoryUsageMetricId, 46,
                                         "component6", 606));
  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 46, "component6", 606,
                                            observation_store_.get(), update_recipient_.get()));
}

// Tests the version of the method LogMemoryUsage() that accepts a vector of
// event codes.
TEST_F(LoggerTest, LogMemoryUsageMultiDimension) {
  std::vector<uint32_t> expected_report_ids = {
      testing::all_report_types::kLedgerMemoryUsageLedgerMemoryUsageAggregatedReportId,
      testing::all_report_types::kLedgerMemoryUsageLedgerMemoryUsageHistogramReportId};

  // Use no event codes when the metric has two dimension. Expect kOK.
  ASSERT_EQ(kOK, logger_->LogMemoryUsage(testing::all_report_types::kLedgerMemoryUsageMetricId,
                                         std::vector<uint32_t>({}), "component6", 606));

  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 0, "component6", 606,
                                            observation_store_.get(), update_recipient_.get()));
  ResetObservationStore();

  // Use one event code when the metric has two dimension. Expect kOK.
  ASSERT_EQ(kOK, logger_->LogMemoryUsage(testing::all_report_types::kLedgerMemoryUsageMetricId,
                                         std::vector<uint32_t>({45}), "component6", 606));
  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, 45, "component6", 606,
                                            observation_store_.get(), update_recipient_.get()));
  ResetObservationStore();

  // Use three event codes when the metric has two dimension. Expect
  // kInvalidArguments.
  ASSERT_EQ(kInvalidArguments,
            logger_->LogMemoryUsage(testing::all_report_types::kLedgerMemoryUsageMetricId,
                                    std::vector<uint32_t>({45, 46, 47}), "component6", 606));

  // All good
  ASSERT_EQ(kOK, logger_->LogMemoryUsage(testing::all_report_types::kLedgerMemoryUsageMetricId,
                                         std::vector<uint32_t>({1, 46}), "component6", 606));
  uint64_t expected_packed_event_code = (1) | (46 << 10);

  EXPECT_TRUE(CheckNumericEventObservations(expected_report_ids, expected_packed_event_code,
                                            "component6", 606, observation_store_.get(),
                                            update_recipient_.get()));
}

// Tests the method LogIntHistogram().
TEST_F(LoggerTest, LogIntHistogram) {
  std::vector<uint32_t> indices = {0, 1, 2, 3};
  std::vector<uint32_t> counts = {100, 101, 102, 103};
  auto histogram = testing::NewHistogram(indices, counts);
  ASSERT_EQ(kOK, logger_->LogIntHistogram(testing::all_report_types::kFileSystemWriteTimesMetricId,
                                          47, "component7", std::move(histogram)));
  Observation2 observation;
  uint32_t expected_report_id =
      testing::all_report_types::kFileSystemWriteTimesFileSystemWriteTimesHistogramReportId;
  ASSERT_TRUE(FetchSingleObservation(&observation, expected_report_id, observation_store_.get(),
                                     update_recipient_.get()));
  ASSERT_TRUE(observation.has_histogram());
  auto histogram_observation = observation.histogram();
  EXPECT_EQ(47u, histogram_observation.event_code());
  EXPECT_EQ(histogram_observation.component_name_hash().size(), 32u);
  EXPECT_EQ(static_cast<size_t>(histogram_observation.buckets_size()), indices.size());
  for (auto i = 0u; i < indices.size(); i++) {
    const auto& bucket = histogram_observation.buckets(i);
    EXPECT_EQ(bucket.index(), indices[i]);
    EXPECT_EQ(bucket.count(), counts[i]);
  }
}

// Tests the method LogCustomEvent().
TEST_F(LoggerTest, LogCustomEvent) {
  CustomDimensionValue module_value, number_value;
  module_value.set_string_value("gmail");
  number_value.set_int_value(3);
  std::vector<std::string> dimension_names = {"module", "number"};
  std::vector<CustomDimensionValue> values = {module_value, number_value};
  auto custom_event = testing::NewCustomEvent(dimension_names, values);
  ASSERT_EQ(kOK, logger_->LogCustomEvent(testing::all_report_types::kModuleInstallsMetricId,
                                         std::move(custom_event)));
  Observation2 observation;
  uint32_t expected_report_id =
      testing::all_report_types::kModuleInstallsModuleInstallsDetailedDataReportId;
  ASSERT_TRUE(FetchSingleObservation(&observation, expected_report_id, observation_store_.get(),
                                     update_recipient_.get()));
  ASSERT_TRUE(observation.has_custom());
  const CustomObservation& custom_observation = observation.custom();
  for (auto i = 0u; i < values.size(); i++) {
    auto obs_dimension = custom_observation.values().at(dimension_names[i]);
#ifdef PROTO_LITE
#else
    EXPECT_TRUE(MessageDifferencer::Equals(obs_dimension, values[i]));
#endif
  }
}

// Tests that the expected number of locally aggregated Observations are
// generated when multiple Events of different types have been logged for
// locally aggregated reports.
TEST_F(LoggerTest, CheckNumAggregatedObsMultipleEvents) {
  auto expected_params = expected_aggregation_params_;
  // Log 2 occurrences of event code 0 for the DeviceBoots metric, which has 1
  // locally aggregated report and no immediate reports.
  ASSERT_EQ(kOK, logger_->LogEvent(testing::all_report_types::kDeviceBootsMetricId, 0));
  ASSERT_EQ(kOK, logger_->LogEvent(testing::all_report_types::kDeviceBootsMetricId, 0));
  // Log 2 occurrences of distinct event codes for the FeaturesActive metric,
  // which has 1 locally aggregated report and no immediate reports.
  ASSERT_EQ(kOK, logger_->LogEvent(testing::all_report_types::kFeaturesActiveMetricId, 0));
  ASSERT_EQ(kOK, logger_->LogEvent(testing::all_report_types::kFeaturesActiveMetricId, 1));
  // Log 2 event counts for event code 0, for distinct components, for the
  // SettingsChanged metric. This metric has 1 locally aggregated report and no
  // immediate reports.
  ASSERT_EQ(kOK, logger_->LogEventCount(testing::all_report_types::kSettingsChangedMetricId, 0,
                                        "component_A", 0, 10));
  ASSERT_EQ(kOK, logger_->LogEventCount(testing::all_report_types::kSettingsChangedMetricId, 0,
                                        "component_B", 0, 15));
  // Check that no immediate Observations were generated.
  std::vector<Observation2> immediate_observations(0);
  std::vector<uint32_t> expected_immediate_report_ids = {};
  ASSERT_TRUE(FetchObservations(&immediate_observations, expected_immediate_report_ids,
                                observation_store_.get(), update_recipient_.get()));
  // Generate locally aggregated observations for the current day index.
  ASSERT_EQ(kOK,
            event_aggregator_mgr_->GenerateObservations(CurrentDayIndex(MetricDefinition::UTC)));
  // Update |expected_aggregation_params_| to account for the events logged by
  // this test. In addition to the initial values, we expect 1 Observation for
  // the SettingsChanged_PerDeviceCount report, for each of the 2 window sizes
  // of the report, for each of the 2 components appearing in logged events.
  expected_aggregation_params_.daily_num_obs += 4;
  expected_aggregation_params_
      .num_obs_per_report[testing::all_report_types::kSettingsChangedMetricReportId] += 4;
  // Check that the expected numbers of aggregated observations were
  // generated.
  std::vector<Observation2> aggregated_observations;
  EXPECT_TRUE(FetchAggregatedObservations(&aggregated_observations, expected_aggregation_params_,
                                          observation_store_.get(), update_recipient_.get()));
}

TEST_F(LoggerTest, TestPausingLogging) {
  ASSERT_EQ(internal_logger_->call_count(), 0);
  ASSERT_EQ(kOK, logger_->LogEvent(testing::all_report_types::kErrorOccurredMetricId, 42));
  ASSERT_EQ(internal_logger_->call_count(), 2);
  logger_->PauseInternalLogging();
  ASSERT_EQ(kOK, logger_->LogEvent(testing::all_report_types::kErrorOccurredMetricId, 42));
  ASSERT_EQ(internal_logger_->call_count(), 2);
  logger_->ResumeInternalLogging();
  ASSERT_EQ(kOK, logger_->LogEvent(testing::all_report_types::kErrorOccurredMetricId, 42));
  ASSERT_EQ(internal_logger_->call_count(), 4);
}

// Tests the events are not sent to the UndatedEventManager.
TEST_F(LoggerTest, AccurateClockEventsLogged) {
  validated_clock_->SetAccurate(true);
  ASSERT_EQ(kOK, logger_->LogEvent(testing::all_report_types::kErrorOccurredMetricId, 42));
  // Check that only an Observation is generated, no events are cached.
  CHECK_EQ(1, observation_store_->num_observations_added());
  CHECK_EQ(0, undated_event_manager_->NumSavedEvents());
}

// Tests the diversion of events to the UndatedEventManager.
TEST_F(LoggerTest, InaccurateClockEventsSaved) {
  validated_clock_->SetAccurate(false);
  ASSERT_EQ(kOK, logger_->LogEvent(testing::all_report_types::kErrorOccurredMetricId, 42));
  // Check that no immediate Observations were generated, and an event is cached.
  CHECK_EQ(0, observation_store_->num_observations_added());
  CHECK_EQ(1, undated_event_manager_->NumSavedEvents());
}

// Tests the diversion of events to the UndatedEventManager stops once the clock becomes accurate.
TEST_F(LoggerTest, InaccurateClockEventsSavedOnlyWhileClockIsInaccurate) {
  validated_clock_->SetAccurate(false);
  ASSERT_EQ(kOK, logger_->LogEvent(testing::all_report_types::kErrorOccurredMetricId, 42));
  // Check that no immediate Observations were generated, and an event is cached.
  CHECK_EQ(0, observation_store_->num_observations_added());
  CHECK_EQ(1, undated_event_manager_->NumSavedEvents());

  // Clock becomes accurate.
  validated_clock_->SetAccurate(true);
  ASSERT_EQ(kOK, logger_->LogEvent(testing::all_report_types::kErrorOccurredMetricId, 42));
  // Check that now Observations are generated, the one event is still cached.
  CHECK_EQ(1, observation_store_->num_observations_added());
  CHECK_EQ(1, undated_event_manager_->NumSavedEvents());
}

// Tests the error when the UndatedEventManager was deleted early and the clock is invalid.
TEST_F(LoggerTest, AlreadyDeletedError) {
  validated_clock_->SetAccurate(false);
  // Delete the UndatedEventManager.
  undated_event_manager_.reset();
  // Error is returned because the clock should become accurate if the UndatedEventManager is
  // deleted.
  ASSERT_EQ(kOther, logger_->LogEvent(testing::all_report_types::kErrorOccurredMetricId, 42));
  // Check that no immediate Observations were generated, and no events are cached.
  CHECK_EQ(0, observation_store_->num_observations_added());
}

// Tests the race condition when the UndatedEventManager is deleted and the clock becomes valid.
TEST_F(LoggerTest, AlreadyDeletedRaceCondition) {
  // Clock is initially invalid, but becomes valid on subsequent attempts.
  validated_clock_->SetAccurate({false, true});
  // Delete the UndatedEventManager.
  undated_event_manager_.reset();
  ASSERT_EQ(kOK, logger_->LogEvent(testing::all_report_types::kErrorOccurredMetricId, 42));
  // Check that the immediate Observation was generated.
  CHECK_EQ(1, observation_store_->num_observations_added());
}

// Tests the UndatedEventManager forwarding events when it has been flushed before the call to
// Save().
TEST_F(LoggerTest, AlreadyFlushedError) {
  validated_clock_->SetAccurate(false);
  IncrementingSystemClock system_clock;
  undated_event_manager_->Flush(&system_clock, internal_logger_.get());
  ASSERT_EQ(kOK, logger_->LogEvent(testing::all_report_types::kErrorOccurredMetricId, 42));
  // Check that one immediate Observation was generated, and no events are cached.
  CHECK_EQ(1, observation_store_->num_observations_added());
  CHECK_EQ(0, undated_event_manager_->NumSavedEvents());
}

// Tests the race condition when the UndatedEventManager is being flushed while the event is being
// logged.
TEST_F(LoggerTest, ClockBecomesAccurateRaceCondition) {
  // Clock is initially invalid, but becomes valid on subsequent attempts.
  validated_clock_->SetAccurate({false, true});
  IncrementingSystemClock system_clock;
  undated_event_manager_->Flush(&system_clock, internal_logger_.get());
  ASSERT_EQ(kOK, logger_->LogEvent(testing::all_report_types::kErrorOccurredMetricId, 42));
  // Check that an immediate Observation is generated, and no events are cached after Flush.
  CHECK_EQ(1, observation_store_->num_observations_added());
  CHECK_EQ(0, undated_event_manager_->NumSavedEvents());
}

class NoValidatedClockLoggerTest : public util::testing::TestWithFiles {
 protected:
  void SetUp() override {
    MakeTestFolder();
    observation_store_ = std::make_unique<FakeObservationStore>();
    update_recipient_ = std::make_unique<TestUpdateRecipient>();
    observation_encrypter_ = EncryptedMessageMaker::MakeUnencrypted();
    observation_writer_ = std::make_unique<ObservationWriter>(
        observation_store_.get(), update_recipient_.get(), observation_encrypter_.get());
    encoder_ = std::make_unique<Encoder>(ClientSecret::GenerateNewSecret(), system_data_.get());
    CobaltConfig cfg = {.client_secret = system_data::ClientSecret::GenerateNewSecret()};

    cfg.local_aggregation_backfill_days = 0;
    cfg.local_aggregate_proto_store_path = aggregate_store_path();
    cfg.obs_history_proto_store_path = obs_history_path();

    event_aggregator_mgr_ = std::make_unique<TestEventAggregatorManager>(cfg, fs(), encoder_.get(),
                                                                         observation_writer_.get());
    internal_logger_ = std::make_unique<testing::FakeLogger>();

    logger_ = std::make_unique<Logger>(
        GetTestProject(testing::all_report_types::kCobaltRegistryBase64), encoder_.get(),
        event_aggregator_mgr_->GetEventAggregator(), observation_writer_.get(), system_data_.get(),
        internal_logger_.get());
  }

  void TearDown() override {
    event_aggregator_mgr_.reset();
    logger_.reset();
  }

  std::unique_ptr<Logger> logger_;
  std::unique_ptr<testing::FakeLogger> internal_logger_;
  std::unique_ptr<ObservationWriter> observation_writer_;
  std::unique_ptr<FakeObservationStore> observation_store_;
  std::unique_ptr<TestUpdateRecipient> update_recipient_;

 private:
  std::unique_ptr<Encoder> encoder_;
  std::unique_ptr<EventAggregatorManager> event_aggregator_mgr_;
  std::unique_ptr<EncryptedMessageMaker> observation_encrypter_;
  std::unique_ptr<SystemDataInterface> system_data_;
};

// Tests the events are logged to the event loggers.
TEST_F(NoValidatedClockLoggerTest, AccurateClockEventsLogged) {
  ASSERT_EQ(kOK, logger_->LogEvent(testing::all_report_types::kErrorOccurredMetricId, 42));
  // Check that an Observation is generated.
  CHECK_EQ(1, observation_store_->num_observations_added());
  // Verify a reasonable day index is generated by the logger's system clock.
  CHECK_GT(observation_store_->metadata_received[0]->day_index(), 18000);
}

}  // namespace cobalt::logger

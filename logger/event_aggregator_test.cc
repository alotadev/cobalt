// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/event_aggregator.h"

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "./event.pb.h"
#include "./gtest.h"
#include "logger/logger_test_utils.h"
#include "util/clock.h"
#include "util/datetime_util.h"
#include "util/proto_util.h"

using ::google::protobuf::util::MessageDifferencer;

namespace cobalt {

using encoder::ClientSecret;
using encoder::ObservationStoreUpdateRecipient;
using encoder::ObservationStoreWriterInterface;
using encoder::SystemDataInterface;
using util::EncryptedMessageMaker;
using util::IncrementingClock;
using util::SerializeToBase64;
using util::TimeToDayIndex;

namespace logger {

using testing::CheckPerDeviceCountObservations;
using testing::CheckUniqueActivesObservations;
using testing::ExpectedAggregationParams;
using testing::ExpectedPerDeviceCountObservations;
using testing::ExpectedReportParticipationObservations;
using testing::ExpectedUniqueActivesObservations;
using testing::FakeObservationStore;
using testing::FetchAggregatedObservations;
using testing::MakeAggregationConfig;
using testing::MakeAggregationKey;
using testing::MakeExpectedReportParticipationObservations;
using testing::MakeNullExpectedUniqueActivesObservations;
using testing::MockConsistentProtoStore;
using testing::PopulateMetricDefinitions;
using testing::TestUpdateRecipient;

namespace {
// Number of seconds in a day
const int kDay = 60 * 60 * 24;
// Number of seconds in an ideal year
const int kYear = kDay * 365;

static const uint32_t kCustomerId = 1;
static const uint32_t kProjectId = 1;
static const char kCustomerName[] = "Fuchsia";
static const char kProjectName[] = "Cobalt";

// Filenames for constructors of ConsistentProtoStores
static const char kAggregateStoreFilename[] = "local_aggregate_store_backup";
static const char kObsHistoryFilename[] = "obs_history_backup";

// Pairs (metric ID, report ID) for the locally aggregated reports defined
// in the MetricDefinition strings below. The MetricDefinition and
// ReportDefinition corresponding to each of these MetricReportIds is determined
// relative to a ProjectContext.
const MetricReportId kDeviceBootsMetricReportId = MetricReportId(10, 101);
const MetricReportId kFeaturesActiveMetricReportId = MetricReportId(20, 201);
const MetricReportId kErrorsOccurredMetricReportId = MetricReportId(30, 302);
const MetricReportId kEventsOccurredMetricReportId = MetricReportId(40, 402);
const MetricReportId kConnectionFailuresMetricReportId =
    MetricReportId(50, 501);
const MetricReportId kSettingsChangedMetricReportId = MetricReportId(60, 601);

// A set of metric definitions of various types, each with a locally aggregated
// report.
static const char kMetricDefinitions[] = R"(
metric {
  metric_name: "ErrorsOccurred"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 30
  max_event_code: 2
  reports: {
    report_name: "ErrorsOccurred_SimpleCount"
    id: 301
    report_type: SIMPLE_OCCURRENCE_COUNT
    local_privacy_noise_level: NONE
  }
  reports: {
    report_name: "ErrorsOccurred_UniqueDevices"
    id: 302
    report_type: UNIQUE_N_DAY_ACTIVES
    local_privacy_noise_level: LARGE
    window_size: 1
    window_size: 7
    window_size: 30
  }
}

metric {
  metric_name: "ConnectionFailures"
  metric_type: EVENT_COUNT
  customer_id: 1
  project_id: 1
  id: 50
  reports: {
    report_name: "ConnectionFailures_PerDeviceCount"
    id: 501
    report_type: PER_DEVICE_COUNT_STATS
    window_size: 1
  }
}

)";

// Properties of the locally aggregated Observations which should be generated
// for the reports in |kMetricDefinitions|, assuming that no events have ever
// been logged for those reports.
static const ExpectedAggregationParams kExpectedParams = {
    /* The total number of locally aggregated Observations that should be
       generated for each day index. */
    10,
    /* The MetricReportIds of the locally aggregated reports in this
       configuration. */
    {kErrorsOccurredMetricReportId, kConnectionFailuresMetricReportId},
    /* The number of Observations which should be generated for each day index,
       broken down by MetricReportId. */
    {{kErrorsOccurredMetricReportId, 9},
     {kConnectionFailuresMetricReportId, 1}},
    /* The number of event codes for each report of type UNIQUE_N_DAY_ACTIVES,
       by MetricReportId. */
    {{kErrorsOccurredMetricReportId, 3}},
    /* The set of window sizes for each MetricReportId. */
    {{kErrorsOccurredMetricReportId, {1, 7, 30}},
     {kConnectionFailuresMetricReportId, {1}}}};

// A set of metric definitions of type EVENT_OCCURRED, each of which has a
// UNIQUE_N_DAY_ACTIVES report.
static const char kUniqueActivesMetricDefinitions[] = R"(
metric {
  metric_name: "DeviceBoots"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 10
  max_event_code: 1
  reports: {
    report_name: "DeviceBoots_UniqueDevices"
    id: 101
    report_type: UNIQUE_N_DAY_ACTIVES
    local_privacy_noise_level: MEDIUM
    window_size: 1
  }
}

metric {
  metric_name: "FeaturesActive"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 20
  max_event_code: 4
  reports: {
    report_name: "FeaturesActive_UniqueDevices"
    id: 201
    report_type: UNIQUE_N_DAY_ACTIVES
    local_privacy_noise_level: LARGE
    window_size: 7
    window_size: 30
  }
}

metric {
  metric_name: "ErrorsOccurred"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 30
  max_event_code: 2
  reports: {
    report_name: "ErrorsOccurred_SimpleCount"
    id: 301
    report_type: SIMPLE_OCCURRENCE_COUNT
    local_privacy_noise_level: NONE
  }
  reports: {
    report_name: "ErrorsOccurred_UniqueDevices"
    id: 302
    report_type: UNIQUE_N_DAY_ACTIVES
    local_privacy_noise_level: LARGE
    window_size: 1
    window_size: 7
    window_size: 30
  }
}

)";

// Properties of the locally aggregated reports in
// |kUniqueActivesMetricDefinitions|.
static const ExpectedAggregationParams kUniqueActivesExpectedParams = {
    /* The total number of locally aggregated Observations which should be
       generated for each day index. */
    21,
    /* The MetricReportIds of the locally aggregated reports in this
       configuration. */
    {kDeviceBootsMetricReportId, kFeaturesActiveMetricReportId,
     kErrorsOccurredMetricReportId},
    /* The number of Observations which should be generated for each day
       index, broken down by MetricReportId. */
    {{kDeviceBootsMetricReportId, 2},
     {kFeaturesActiveMetricReportId, 10},
     {kErrorsOccurredMetricReportId, 9}},
    /* The number of event codes for each MetricReportId. */
    {{kDeviceBootsMetricReportId, 2},
     {kFeaturesActiveMetricReportId, 5},
     {kErrorsOccurredMetricReportId, 3}},
    /* The set of window sizes for each MetricReportId. */
    {{kDeviceBootsMetricReportId, {1}},
     {kFeaturesActiveMetricReportId, {7, 30}},
     {kErrorsOccurredMetricReportId, {1, 7, 30}}}};

// A set of MetricDefinitions of type EVENT_OCCURRED, each of which has a
// UNIQUE_N_DAY_ACTIVES report with local_privacy_noise_level set to NONE.
static const char kNoiseFreeUniqueActivesMetricDefinitions[] = R"(
metric {
  metric_name: "DeviceBoots"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 10
  max_event_code: 1
  reports: {
    report_name: "DeviceBoots_UniqueDevices"
    id: 101
    report_type: UNIQUE_N_DAY_ACTIVES
    local_privacy_noise_level: NONE
    window_size: 1
  }
}

metric {
  metric_name: "FeaturesActive"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 20
  max_event_code: 4
  reports: {
    report_name: "FeaturesActive_UniqueDevices"
    id: 201
    report_type: UNIQUE_N_DAY_ACTIVES
    local_privacy_noise_level: NONE
    window_size: 1
    window_size: 7
    window_size: 30
  }
}

metric {
  metric_name: "EventsOccurred"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 40
  max_event_code: 4
  reports: {
    report_name: "EventsOccurred_SimpleCount"
    id: 401
    report_type: SIMPLE_OCCURRENCE_COUNT
    local_privacy_noise_level: NONE
  }
  reports: {
    report_name: "EventsOccurred_UniqueDevices"
    id: 402
    report_type: UNIQUE_N_DAY_ACTIVES
    local_privacy_noise_level: NONE
    window_size: 1
    window_size: 7
  }
}
)";

// Properties of the locally aggregated reports in
// |kNoiseFreeUniqueActivesMetricDefinitions|.
static const ExpectedAggregationParams kNoiseFreeUniqueActivesExpectedParams = {
    /* The total number of locally aggregated Observations which should be
       generated for each day index. */
    27,
    /* The MetricReportIds of the locally aggregated reports in this
configuration. */
    {kDeviceBootsMetricReportId, kFeaturesActiveMetricReportId,
     kEventsOccurredMetricReportId},
    /* The number of Observations which should be generated for each day
       index, broken down by MetricReportId. */
    {{kDeviceBootsMetricReportId, 2},
     {kFeaturesActiveMetricReportId, 15},
     {kEventsOccurredMetricReportId, 10}},
    /* The number of event codes for each MetricReportId. */
    {{kDeviceBootsMetricReportId, 2},
     {kFeaturesActiveMetricReportId, 5},
     {kEventsOccurredMetricReportId, 5}},
    /* The set of window sizes for each MetricReportId. */
    {{kDeviceBootsMetricReportId, {1}},
     {kFeaturesActiveMetricReportId, {1, 7, 30}},
     {kEventsOccurredMetricReportId, {1, 7}}}};

// A set of MetricDefinitions of type EVENT_COUNT, each of which has a
// ReportDefinition of type PER_DEVICE_COUNT_STATS.
static const char kPerDeviceCountMetricDefinitions[] = R"(
metric {
  metric_name: "ConnectionFailures"
  metric_type: EVENT_COUNT
  customer_id: 1
  project_id: 1
  id: 50
  reports: {
    report_name: "ConnectionFailures_PerDeviceCount"
    id: 501
    report_type: PER_DEVICE_COUNT_STATS
    window_size: 1
  }
}

metric {
  metric_name: "SettingsChanged"
  metric_type: EVENT_COUNT
  customer_id: 1
  project_id: 1
  id: 60
  reports: {
    report_name: "SettingsChanged_PerDeviceCount"
    id: 601
    report_type: PER_DEVICE_COUNT_STATS
    window_size: 7
    window_size: 30
  }
}

)";

// Properties of the locally aggregated Observations which should be generated
// for the reports in |kMetricDefinitions|, assuming that no events have ever
// been logged for those reports.
static const ExpectedAggregationParams kPerDeviceCountExpectedParams = {
    /* The total number of Observations that should be generated for a day
       index. */
    2,
    /* The MetricReportIds of the locally aggregated reports in this
       configuration. */
    {kConnectionFailuresMetricReportId, kSettingsChangedMetricReportId},
    /* The number of Observations which should be generated for a day index,
       broken down by MetricReportId. */
    {{kConnectionFailuresMetricReportId, 1},
     {kSettingsChangedMetricReportId, 1}},
    /* The number of event codes for each UNIQUE_N_DAY_ACTIVES report. Omitted
       because this config contains no UNIQUE_N_DAY_ACTIVES reports. */
    {},
    /* The set of window sizes for each MetricReportId. */
    {{kConnectionFailuresMetricReportId, {1}},
     {kSettingsChangedMetricReportId, {7, 30}}}};

// A set of MetricDefinitions including one with TimeZonePolicy UTC and one with
// TimeZonePolicy LOCAL.
static const char kNoiseFreeMixedTimeZoneMetricDefinitions[] = R"(
metric {
  metric_name: "DeviceBoots"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 10
  max_event_code: 2
  time_zone_policy: LOCAL
  reports: {
    report_name: "DeviceBoots_UniqueDevices"
    id: 101
    report_type: UNIQUE_N_DAY_ACTIVES
    local_privacy_noise_level: NONE
    window_size: 1
  }
}

metric {
  metric_name: "FeaturesActive"
  metric_type: EVENT_OCCURRED
  customer_id: 1
  project_id: 1
  id: 20
  max_event_code: 2
  time_zone_policy: UTC
  reports: {
    report_name: "FeaturesActive_UniqueDevices"
    id: 201
    report_type: UNIQUE_N_DAY_ACTIVES
    local_privacy_noise_level: NONE
    window_size: 1
  }
}

)";

static const ExpectedAggregationParams kNoiseFreeMixedTimeZoneExpectedParams = {
    /* The total number of locally aggregated Observations which should be
       generated for each day index. */
    6,
    /* The MetricReportIds of the locally aggregated reports in this
configuration. */
    {kDeviceBootsMetricReportId, kFeaturesActiveMetricReportId},
    /* The number of Observations which should be generated for each day
       index, broken down by MetricReportId. */
    {{kDeviceBootsMetricReportId, 3}, {kFeaturesActiveMetricReportId, 3}},
    /* The number of event codes for each MetricReportId. */
    {{kDeviceBootsMetricReportId, 3}, {kFeaturesActiveMetricReportId, 3}},
    /* The set of window sizes for each MetricReportId. */
    {{kDeviceBootsMetricReportId, {1}}, {kFeaturesActiveMetricReportId, {1}}}};

// A map keyed by base64-encoded, serialized ReportAggregationKeys. The value at
// a key is a map of event codes to sets of day indices. Used in tests as
// a record, external to the LocalAggregateStore, of the activity logged for
// UNIQUE_N_DAY_ACTIVES reports.
typedef std::map<std::string, std::map<uint32_t, std::set<uint32_t>>>
    LoggedActivity;

// A map used in tests as a record, external to the LocalAggregateStore, of the
// activity logged for PER_DEVICE_COUNT_STATS reports. The keys are, in
// descending order, serialized ReportAggregationKeys, components, event codes,
// and day indices. The innermost value is a count.
typedef std::map<
    std::string,
    std::map<std::string, std::map<uint32_t, std::map<uint32_t, int64_t>>>>
    LoggedCounts;

// Given a string representing a MetricDefinitions proto message, creates a
// ProjectContext from that MetricDefinitions and returns a unique pointer.
std::unique_ptr<ProjectContext> MakeProjectContext(const char metric_string[]) {
  auto metric_definitions = std::make_unique<MetricDefinitions>();
  if (!PopulateMetricDefinitions(metric_string, metric_definitions.get())) {
    return nullptr;
  }
  auto project_context = std::make_unique<ProjectContext>(
      kCustomerId, kProjectId, kCustomerName, kProjectName,
      std::move(metric_definitions));
  return project_context;
}

}  // namespace

// EventAggregatorTest creates an EventAggregator which sends its Observations
// to a FakeObservationStore. The EventAggregator is not pre-populated with
// aggregation configurations.
class EventAggregatorTest : public ::testing::Test {
 protected:
  void SetUp() {
    observation_store_.reset(new FakeObservationStore);
    update_recipient_.reset(new TestUpdateRecipient);
    observation_encrypter_ = EncryptedMessageMaker::MakeUnencrypted();
    observation_writer_.reset(
        new ObservationWriter(observation_store_.get(), update_recipient_.get(),
                              observation_encrypter_.get()));
    encoder_.reset(
        new Encoder(ClientSecret::GenerateNewSecret(), system_data_.get()));
    local_aggregate_proto_store_.reset(
        new MockConsistentProtoStore(kAggregateStoreFilename));
    obs_history_proto_store_.reset(
        new MockConsistentProtoStore(kObsHistoryFilename));
    event_aggregator_.reset(new EventAggregator(
        encoder_.get(), observation_writer_.get(),
        local_aggregate_proto_store_.get(), obs_history_proto_store_.get()));
    // Provide the EventAggregator with a mock clock starting at 10 years after
    // the beginning of time.
    mock_clock_ = new IncrementingClock(std::chrono::system_clock::duration(0));
    mock_clock_->set_time(std::chrono::system_clock::time_point(
        std::chrono::seconds(10 * kYear)));
    event_aggregator_->SetClock(mock_clock_);
    day_store_created_ = CurrentDayIndex();
  }

  // Destruct the EventAggregator (thus calling EventAggregator::ShutDown())
  // before destructing the objects which the EventAggregator points to but does
  // not own.
  void TearDown() { event_aggregator_.reset(); }

  // Advances |mock_clock_| by |num_seconds| seconds.
  void AdvanceClock(int num_seconds) {
    mock_clock_->increment_by(std::chrono::seconds(num_seconds));
  }

  // Returns the day index of the current day according to |mock_clock_|, in
  // |time_zone|, without incrementing the clock.
  uint32_t CurrentDayIndex(
      MetricDefinition::TimeZonePolicy time_zone = MetricDefinition::UTC) {
    return TimeToDayIndex(
        std::chrono::system_clock::to_time_t(mock_clock_->peek_now()),
        time_zone);
  }

  size_t GetBackfillDays() { return event_aggregator_->backfill_days_; }

  void SetBackfillDays(size_t num_days) {
    event_aggregator_->backfill_days_ = num_days;
  }

  Status BackUpLocalAggregateStore() {
    return event_aggregator_->BackUpLocalAggregateStore();
  }

  Status BackUpObservationHistory() {
    return event_aggregator_->BackUpObservationHistory();
  }

  LocalAggregateStore CopyLocalAggregateStore() {
    return event_aggregator_->CopyLocalAggregateStore();
  }

  Status GenerateObservations(uint32_t final_day_index_utc,
                              uint32_t final_day_index_local = 0u) {
    return event_aggregator_->GenerateObservationsNoWorker(
        final_day_index_utc, final_day_index_local);
  }

  Status GarbageCollect(uint32_t day_index_utc, uint32_t day_index_local = 0u) {
    return event_aggregator_->GarbageCollect(day_index_utc, day_index_local);
  }

  void DoScheduledTasksNow() {
    auto current_time = mock_clock_->now();
    event_aggregator_->next_generate_obs_ = current_time;
    event_aggregator_->next_gc_ = current_time;
    event_aggregator_->DoScheduledTasks(current_time);
  }

  // Clears the FakeObservationStore and resets the counts of Observations
  // received by the FakeObservationStore and the TestUpdateRecipient.
  void ResetObservationStore() {
    observation_store_->messages_received.clear();
    observation_store_->metadata_received.clear();
    observation_store_->ResetObservationCounter();
    update_recipient_->invocation_count = 0;
  }

  void ResetProtoStores() {
    local_aggregate_proto_store_->ResetCounts();
    obs_history_proto_store_->ResetCounts();
  }

  // Given a ProjectContext |project_context| and the MetricReportId of a
  // UNIQUE_N_DAY_ACTIVES report in |project_context|, as well as a day index
  // and an event code, logs an OccurrenceEvent to the EventAggregator for
  // that report, day index, and event code. If a non-null LoggedActivity map is
  // provided, updates the map with information about the logged Event.
  Status LogUniqueActivesEvent(const ProjectContext& project_context,
                               const MetricReportId& metric_report_id,
                               uint32_t day_index, uint32_t event_code,
                               LoggedActivity* logged_activity = nullptr) {
    EventRecord event_record;
    event_record.metric = project_context.GetMetric(metric_report_id.first);
    event_record.event->set_day_index(day_index);
    event_record.event->mutable_occurrence_event()->set_event_code(event_code);
    auto status = event_aggregator_->LogUniqueActivesEvent(
        metric_report_id.second, &event_record);
    if (logged_activity == nullptr) {
      return status;
    }
    std::string key;
    if (!SerializeToBase64(
            MakeAggregationKey(project_context, metric_report_id), &key)) {
      return kInvalidArguments;
    }
    (*logged_activity)[key][event_code].insert(day_index);
    return status;
  }

  // Given a ProjectContext |project_context| and the MetricReportId of an
  // PER_DEVICE_COUNT_STATS report in |project_context|, as well as a
  // day index, a component string, and an event code, logs a CountEvent to the
  // EventAggregator for that report, day index, component, and event code. If a
  // non-null LoggedCounts map is provided, updates the map with information
  // about the logged Event.
  Status LogPerDeviceCountEvent(const ProjectContext& project_context,
                                const MetricReportId& metric_report_id,
                                uint32_t day_index,
                                const std::string& component,
                                uint32_t event_code, int64_t count,
                                LoggedCounts* logged_counts = nullptr) {
    EventRecord event_record;
    event_record.metric = project_context.GetMetric(metric_report_id.first);
    event_record.event->set_day_index(day_index);
    auto count_event = event_record.event->mutable_count_event();
    count_event->set_component(component);
    count_event->add_event_code(event_code);
    count_event->set_count(count);
    auto status = event_aggregator_->LogPerDeviceCountEvent(
        metric_report_id.second, &event_record);
    if (logged_counts == nullptr) {
      return status;
    }
    std::string key;
    if (!SerializeToBase64(
            MakeAggregationKey(project_context, metric_report_id), &key)) {
      return kInvalidArguments;
    }
    (*logged_counts)[key][component][event_code][day_index] += count;
    return status;
  }

  // Given a LoggedActivity map describing the events that have been logged
  // to the EventAggregator, checks whether the contents of the
  // LocalAggregateStore are as expected, accounting for any garbage
  // collection.
  //
  // logged_activity: a LoggedActivity representing event occurrences
  // since the LocalAggregateStore was created. All day indices should be
  // greater than or equal to |day_store_created_| and less than or equal to
  // |current_day_index|.
  //
  // current_day_index: The day index of the current day in the test's frame
  // of reference.
  bool CheckUniqueActivesAggregates(const LoggedActivity& logged_activity,
                                    uint32_t current_day_index) {
    auto local_aggregate_store = event_aggregator_->CopyLocalAggregateStore();
    // Check that the LocalAggregateStore contains no more UniqueActives
    // aggregates than |logged_activity| and |day_last_garbage_collected_|
    // should imply.
    for (const auto& report_pair : local_aggregate_store.by_report_key()) {
      const auto& aggregates = report_pair.second;
      if (aggregates.type_case() !=
          ReportAggregates::kUniqueActivesAggregates) {
        continue;
      }
      const auto& report_key = report_pair.first;
      // Check whether this ReportAggregationKey is in |logged_activity|. If
      // not, expect that its by_event_code map is empty.
      auto report_activity = logged_activity.find(report_key);
      if (report_activity == logged_activity.end()) {
        EXPECT_TRUE(
            aggregates.unique_actives_aggregates().by_event_code().empty());
        if (!aggregates.unique_actives_aggregates().by_event_code().empty()) {
          return false;
        }
        break;
      }
      auto expected_events = report_activity->second;
      for (const auto& event_pair :
           aggregates.unique_actives_aggregates().by_event_code()) {
        // Check that this event code is in |logged_activity| under this
        // ReportAggregationKey.
        auto event_code = event_pair.first;
        auto event_activity = expected_events.find(event_code);
        EXPECT_NE(event_activity, expected_events.end());
        if (event_activity == expected_events.end()) {
          return false;
        }
        const auto& expected_days = event_activity->second;
        for (const auto& day_pair : event_pair.second.by_day_index()) {
          // Check that this day index is in |logged_activity| under this
          // ReportAggregationKey and event code.
          const auto& day_index = day_pair.first;
          auto day_activity = expected_days.find(day_index);
          EXPECT_NE(day_activity, expected_days.end());
          if (day_activity == expected_days.end()) {
            return false;
          }
          // Check that the day index is no earlier than is implied by the
          // dates of store creation and garbage collection.
          EXPECT_GE(day_index,
                    EarliestAllowedDayIndex(aggregates.aggregation_config()));
          if (day_index <
              EarliestAllowedDayIndex(aggregates.aggregation_config())) {
            return false;
          }
        }
      }
    }

    // Check that the LocalAggregateStore contains aggregates for all events in
    // |logged_activity|, as long as they are recent enough to have survived any
    // garbage collection.
    for (const auto& logged_pair : logged_activity) {
      const auto& logged_key = logged_pair.first;
      const auto& logged_event_map = logged_pair.second;
      // Check that this ReportAggregationKey is in the LocalAggregateStore, and
      // that the aggregates are of the expected type.
      auto report_aggregates =
          local_aggregate_store.by_report_key().find(logged_key);
      EXPECT_NE(report_aggregates, local_aggregate_store.by_report_key().end());
      if (report_aggregates == local_aggregate_store.by_report_key().end()) {
        return false;
      }
      if (report_aggregates->second.type_case() !=
          ReportAggregates::kUniqueActivesAggregates) {
        return false;
      }
      // Compute the earliest day index that should appear among the aggregates
      // for this report.
      auto earliest_allowed = EarliestAllowedDayIndex(
          report_aggregates->second.aggregation_config());
      for (const auto& logged_event_pair : logged_event_map) {
        const auto& logged_event_code = logged_event_pair.first;
        const auto& logged_days = logged_event_pair.second;
        // Check whether this event code is in the LocalAggregateStore
        // under this ReportAggregationKey. If not, check that all day indices
        // for this event code are smaller than the day index of the earliest
        // allowed aggregate.
        auto event_code_aggregates =
            report_aggregates->second.unique_actives_aggregates()
                .by_event_code()
                .find(logged_event_code);
        if (event_code_aggregates ==
            report_aggregates->second.unique_actives_aggregates()
                .by_event_code()
                .end()) {
          for (auto day_index : logged_days) {
            EXPECT_LT(day_index, earliest_allowed);
            if (day_index >= earliest_allowed) {
              return false;
            }
          }
          break;
        }
        // Check that all of the day indices in |logged_activity| under this
        // ReportAggregationKey and event code are in the
        // LocalAggregateStore, as long as they are recent enough to have
        // survived any garbage collection. Check that each aggregate has its
        // activity field set to true.
        for (const auto& logged_day_index : logged_days) {
          auto day_aggregate =
              event_code_aggregates->second.by_day_index().find(
                  logged_day_index);
          if (logged_day_index >= earliest_allowed) {
            EXPECT_NE(day_aggregate,
                      event_code_aggregates->second.by_day_index().end());
            if (day_aggregate ==
                event_code_aggregates->second.by_day_index().end()) {
              return false;
            }
            EXPECT_TRUE(day_aggregate->second.activity_daily_aggregate()
                            .activity_indicator());
            if (!day_aggregate->second.activity_daily_aggregate()
                     .activity_indicator()) {
              return false;
            }
          }
        }
      }
    }
    return true;
  }

  bool CheckPerDeviceCountAggregates(const LoggedCounts& logged_counts,
                                     uint32_t current_day_index) {
    auto local_aggregate_store = event_aggregator_->CopyLocalAggregateStore();
    // Check that the LocalAggregateStore contains no more PerDeviceCount
    // aggregates than |logged_counts| and |day_last_garbage_collected_| should
    // imply.
    for (const auto& report_pair : local_aggregate_store.by_report_key()) {
      const auto& aggregates = report_pair.second;
      if (aggregates.type_case() != ReportAggregates::kCountAggregates) {
        continue;
      }
      const auto& report_key = report_pair.first;
      // Check whether this ReportAggregationKey is in |logged_counts|. If not,
      // expect that its by_component map is empty.
      auto report_counts = logged_counts.find(report_key);
      if (report_counts == logged_counts.end()) {
        EXPECT_TRUE(aggregates.count_aggregates().by_component().empty());
        if (!aggregates.count_aggregates().by_component().empty()) {
          return false;
        }
        break;
      }
      auto expected_components = report_counts->second;
      for (const auto& component_pair :
           aggregates.count_aggregates().by_component()) {
        // Check that this component is in |logged_counts| under this
        // ReportAggregationKey.
        auto component = component_pair.first;
        auto component_counts = expected_components.find(component);
        EXPECT_NE(component_counts, expected_components.end());
        if (component_counts == expected_components.end()) {
          return false;
        }
        const auto& expected_events = component_counts->second;
        for (const auto& event_pair : component_pair.second.by_event_code()) {
          // Check that this event code is in |logged_counts| under this
          // ReportAggregationKey and component.
          const auto& event_code = event_pair.first;
          auto event_counts = expected_events.find(event_code);
          EXPECT_NE(event_counts, expected_events.end());
          if (event_counts == expected_events.end()) {
            return false;
          }
          const auto& expected_days = event_counts->second;
          for (const auto& day_pair : event_pair.second.by_day_index()) {
            // Check that this day index is in |logged_counts| under this
            // ReportAggregationKey, component, and event code.
            const auto& day_index = day_pair.first;
            auto day_count = expected_days.find(day_index);
            EXPECT_NE(day_count, expected_days.end());
            if (day_count == expected_days.end()) {
              return false;
            }
            // Check that the day index is no earlier than is implied by the
            // dates of store creation and garbage collection.
            EXPECT_GE(day_index,
                      EarliestAllowedDayIndex(aggregates.aggregation_config()));
            if (day_index <
                EarliestAllowedDayIndex(aggregates.aggregation_config())) {
              return false;
            }
          }
        }
      }
    }

    // Check that the LocalAggregateStore contains aggregates for all counts in
    // |logged_counts|, as long as they are recent enough to have survived any
    // garbage collection.
    for (const auto& logged_pair : logged_counts) {
      const auto& logged_key = logged_pair.first;
      const auto& logged_component_map = logged_pair.second;
      // Check that this ReportAggregationKey is in the LocalAggregateStore, and
      // that the aggregates are of the expected type.
      auto report_aggregates =
          local_aggregate_store.by_report_key().find(logged_key);
      EXPECT_NE(report_aggregates, local_aggregate_store.by_report_key().end());
      if (report_aggregates == local_aggregate_store.by_report_key().end()) {
        return false;
      }
      if (report_aggregates->second.type_case() !=
          ReportAggregates::kCountAggregates) {
        return false;
      }
      // Compute the earliest day index that should appear among the aggregates
      // for this report.
      auto earliest_allowed = EarliestAllowedDayIndex(
          report_aggregates->second.aggregation_config());
      for (const auto& logged_component_pair : logged_component_map) {
        const auto& logged_component = logged_component_pair.first;
        const auto& logged_event_code_map = logged_component_pair.second;
        // Check whether this component is in the LocalAggregateStore under this
        // ReportAggregationKey. If not, check that all day indices for all
        // entries in |logged_counts| under this component are smaller than the
        // day index of the earliest allowed aggregate.
        bool component_found = false;
        auto component_aggregates =
            report_aggregates->second.count_aggregates().by_component().find(
                logged_component);
        if (component_aggregates !=
            report_aggregates->second.count_aggregates().by_component().end()) {
          component_found = true;
        }
        for (const auto& logged_event_pair : logged_event_code_map) {
          const auto& logged_event_code = logged_event_pair.first;
          const auto& logged_day_map = logged_event_pair.second;
          // Check whether this event code is in the LocalAggregateStore under
          // this ReportAggregationKey. If not, check that all day indices in
          // |logged_counts| under this component are smaller than the day index
          // of the earliest allowed aggregate.
          bool event_code_found = false;
          if (component_found) {
            auto event_code_aggregates =
                component_aggregates->second.by_event_code().find(
                    logged_event_code);
            if (event_code_aggregates !=
                component_aggregates->second.by_event_code().end()) {
              event_code_found = true;
            }
            if (event_code_found) {
              // Check that all of the day indices in |logged_counts| under this
              // ReportAggregationKey, component, and event code are in the
              // LocalAggregateStore, as long as they are recent enough to have
              // survived any garbage collection. Check that each aggregate has
              // the expected count.
              for (const auto& logged_day_pair : logged_day_map) {
                auto logged_day_index = logged_day_pair.first;
                auto logged_count = logged_day_pair.second;
                auto day_aggregate =
                    event_code_aggregates->second.by_day_index().find(
                        logged_day_index);
                if (logged_day_index >= earliest_allowed) {
                  EXPECT_NE(day_aggregate,
                            event_code_aggregates->second.by_day_index().end());
                  if (day_aggregate ==
                      event_code_aggregates->second.by_day_index().end()) {
                    return false;
                  }
                  EXPECT_EQ(
                      day_aggregate->second.count_daily_aggregate().count(),
                      logged_count);
                  if (day_aggregate->second.count_daily_aggregate().count() !=
                      logged_count) {
                    return false;
                  }
                }
              }
            }
          }
          if (!component_found | !event_code_found) {
            for (auto logged_day_pair : logged_day_map) {
              auto logged_day_index = logged_day_pair.first;
              EXPECT_LT(logged_day_index, earliest_allowed);
              if (logged_day_index >= earliest_allowed) {
                return false;
              }
            }
            break;
          }
        }
      }
    }

    return true;
  }

  // Given the AggregationConfig of a locally aggregated report, returns the
  // earliest (smallest) day index for which an aggregate may exist in the
  // LocalAggregateStore for that report, accounting for garbage
  // collection and the number of backfill days.
  uint32_t EarliestAllowedDayIndex(const AggregationConfig& config) {
    // If the LocalAggregateStore has never been garbage-collected, then the
    // earliest allowed day index is just the day when the store was created,
    // minus the number of backfill days.
    auto backfill_days = GetBackfillDays();
    EXPECT_GE(day_store_created_, backfill_days)
        << "The day index of store creation must be larger than the number "
           "of backfill days.";
    if (day_last_garbage_collected_ == 0u) {
      return day_store_created_ - backfill_days;
    } else {
      // Otherwise, it is the later of:
      // (a) The day index on which the store was created minus the number
      // of backfill days.
      // (b) The day index for which the store was last garbage-collected
      // minus the number of backfill days, minus the largest window size in
      // the report associated to |config|, plus 1.
      EXPECT_GE(day_last_garbage_collected_, backfill_days)
          << "The day index of last garbage collection must be larger than "
             "the number of backfill days.";
      uint32_t max_window_size = 1;
      for (uint32_t window_size : config.report().window_size()) {
        if (window_size > max_window_size) {
          max_window_size = window_size;
        }
      }
      if (day_last_garbage_collected_ - backfill_days < (max_window_size + 1)) {
        return day_store_created_ - backfill_days;
      }
      return (day_store_created_ <
              (day_last_garbage_collected_ - max_window_size + 1))
                 ? (day_last_garbage_collected_ - backfill_days -
                    max_window_size + 1)
                 : day_store_created_ - backfill_days;
    }
  }

  std::unique_ptr<EventAggregator> event_aggregator_;
  std::unique_ptr<MockConsistentProtoStore> local_aggregate_proto_store_;
  std::unique_ptr<MockConsistentProtoStore> obs_history_proto_store_;
  std::unique_ptr<ObservationWriter> observation_writer_;
  std::unique_ptr<Encoder> encoder_;
  std::unique_ptr<EncryptedMessageMaker> observation_encrypter_;
  std::unique_ptr<TestUpdateRecipient> update_recipient_;
  std::unique_ptr<FakeObservationStore> observation_store_;
  IncrementingClock* mock_clock_;
  // The day index on which the LocalAggregateStore was last
  // garbage-collected. A value of 0 indicates that the store has never been
  // garbage-collected.
  uint32_t day_last_garbage_collected_ = 0u;
  // The day index on which the LocalAggregateStore was created.
  uint32_t day_store_created_ = 0u;

 private:
  std::unique_ptr<SystemDataInterface> system_data_;
};

// Creates an EventAggregator and provides it with MetricDefinitions from a
// serialized representation |metric_string|.
class EventAggregatorTestWithProjectContext : public EventAggregatorTest {
 protected:
  explicit EventAggregatorTestWithProjectContext(const char metric_string[]) {
    project_context_ = MakeProjectContext(metric_string);
  }

  void SetUp() {
    EventAggregatorTest::SetUp();
    event_aggregator_->UpdateAggregationConfigs(*project_context_);
  }

  // Logs an OccurrenceEvent for the MetricReportId of a locally
  // aggregated report in |metric_string|. Overrides the method
  // EventAggregatorTest::LogUniqueActivesEvent.
  Status LogUniqueActivesEvent(const MetricReportId& metric_report_id,
                               uint32_t day_index, uint32_t event_code,
                               LoggedActivity* logged_activity = nullptr) {
    return EventAggregatorTest::LogUniqueActivesEvent(
        *project_context_, metric_report_id, day_index, event_code,
        logged_activity);
  }

  // Logs a CountEvent for the MetricReportId of a locally
  // aggregated report in |metric_string|. Overrides the method
  // EventAggregatorTest::LogPerDeviceCountEvent.
  Status LogPerDeviceCountEvent(const MetricReportId& metric_report_id,
                                uint32_t day_index,
                                const std::string& component,
                                uint32_t event_code, int64_t count,
                                LoggedCounts* logged_counts = nullptr) {
    return EventAggregatorTest::LogPerDeviceCountEvent(
        *project_context_, metric_report_id, day_index, component, event_code,
        count, logged_counts);
  }

 private:
  // A ProjectContext wrapping the MetricDefinitions passed to the
  // constructor in |metric_string|.
  std::unique_ptr<ProjectContext> project_context_;
};

// Creates an EventAggregator and provides it with
// |kUniqueActivesMetricDefinitions|.
class UniqueActivesEventAggregatorTest
    : public EventAggregatorTestWithProjectContext {
 protected:
  UniqueActivesEventAggregatorTest()
      : EventAggregatorTestWithProjectContext(kUniqueActivesMetricDefinitions) {
  }
};

// Creates an EventAggregator as in EventAggregatorTest and provides it with
// |kNoiseFreeUniqueActivesMetricDefinitions|.
class NoiseFreeUniqueActivesEventAggregatorTest
    : public EventAggregatorTestWithProjectContext {
 protected:
  NoiseFreeUniqueActivesEventAggregatorTest()
      : EventAggregatorTestWithProjectContext(
            kNoiseFreeUniqueActivesMetricDefinitions) {}
};

// Creates an EventAggregator as in EventAggregatorTest and provides it with
// |kPerDeviceCountMetricDefinitions|.
class PerDeviceCountEventAggregatorTest
    : public EventAggregatorTestWithProjectContext {
 protected:
  PerDeviceCountEventAggregatorTest()
      : EventAggregatorTestWithProjectContext(
            kPerDeviceCountMetricDefinitions) {}
};

// Creates an EventAggregator as in EventAggregatorTest and provides it with
// |kNoiseFreeMixedTimeZoneMetricDefinitions|.
class NoiseFreeMixedTimeZoneEventAggregatorTest
    : public EventAggregatorTestWithProjectContext {
 protected:
  NoiseFreeMixedTimeZoneEventAggregatorTest()
      : EventAggregatorTestWithProjectContext(
            kNoiseFreeMixedTimeZoneMetricDefinitions) {}
};

class EventAggregatorWorkerTest : public EventAggregatorTest {
 protected:
  void SetUp() { EventAggregatorTest::SetUp(); }

  void ShutDownWorkerThread() { event_aggregator_->ShutDown(); }

  bool in_shutdown_state() {
    return (shutdown_flag_set() && !worker_joinable());
  }

  bool in_run_state() { return (!shutdown_flag_set() && worker_joinable()); }

  bool shutdown_flag_set() {
    return event_aggregator_->protected_shutdown_flag_.const_lock()->shut_down;
  }

  bool worker_joinable() {
    return event_aggregator_->worker_thread_.joinable();
  }
};

// Tests that the Read() method of each ConsistentProtoStore is called once
// during construction of the EventAggregator.
TEST_F(EventAggregatorTest, ReadProtosFromFiles) {
  EXPECT_EQ(1, local_aggregate_proto_store_->read_count_);
  EXPECT_EQ(1, obs_history_proto_store_->read_count_);
}

// Tests that the BackUp*() methods return a positive status, and checks that
// the Write() method of a ConsistentProtoStore is called once when its
// respective BackUp*() method is called.
TEST_F(EventAggregatorTest, BackUpProtos) {
  EXPECT_EQ(kOK, BackUpLocalAggregateStore());
  EXPECT_EQ(kOK, BackUpObservationHistory());
  EXPECT_EQ(1, local_aggregate_proto_store_->write_count_);
  EXPECT_EQ(1, obs_history_proto_store_->write_count_);
}

// Tests that an empty LocalAggregateStore is updated with
// ReportAggregationKeys and AggregationConfigs as expected when
// EventAggregator::UpdateAggregationConfigs is called with a ProjectContext
// containing at least one report for each locally aggregated report type.
TEST_F(EventAggregatorTest, UpdateAggregationConfigs) {
  // Check that the LocalAggregateStore is empty.
  EXPECT_EQ(0u, CopyLocalAggregateStore().by_report_key().size());
  // Provide |kMetricDefinitions| to the EventAggregator.
  auto project_context = MakeProjectContext(kMetricDefinitions);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(*project_context));
  // Check that the number of key-value pairs in the LocalAggregateStore is
  // now equal to the number of locally aggregated reports in
  // |kMetricDefinitions|.
  EXPECT_EQ(kExpectedParams.metric_report_ids.size(),
            CopyLocalAggregateStore().by_report_key().size());
  // Check that the LocalAggregateStore contains the expected
  // ReportAggregationKey and AggregationConfig for each locally aggregated
  // report in |kUniqueActivesMetricDefinitions|,
  for (const auto& metric_report_id : kExpectedParams.metric_report_ids) {
    std::string key;
    SerializeToBase64(MakeAggregationKey(*project_context, metric_report_id),
                      &key);
    auto config = MakeAggregationConfig(*project_context, metric_report_id);
    LocalAggregateStore local_aggregate_store = CopyLocalAggregateStore();
    auto report_aggregates = local_aggregate_store.by_report_key().find(key);
    EXPECT_NE(local_aggregate_store.by_report_key().end(), report_aggregates);
    EXPECT_TRUE(MessageDifferencer::Equals(
        config, report_aggregates->second.aggregation_config()));
  }
}

// Tests two assumptions about the behavior of
// EventAggregator::UpdateAggregationConfigs when two projects with the same
// customer ID and project ID provide configurations to the EventAggregator.
// These assumptions are:
// (1) If the second project provides a report with a
// ReportAggregationKey which was not provided by the first project, then
// the EventAggregator accepts the new report. (2) If a report provided by
// the second project has a ReportAggregationKey which was already provided
// by the first project, then the EventAggregator rejects the new report,
// even if its ReportDefinition differs from that of existing report with
// the same ReportAggregationKey.
TEST_F(EventAggregatorTest, UpdateAggregationConfigsWithSameKey) {
  // Provide the EventAggregator with |kUniqueActivesMetricDefinitions|.
  auto unique_actives_project_context =
      MakeProjectContext(kUniqueActivesMetricDefinitions);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(
                     *unique_actives_project_context));
  // Check that the number of key-value pairs in the LocalAggregateStore is
  // now equal to the number of locally aggregated reports in
  // |kUniqueActivesMetricDefinitions|.
  EXPECT_EQ(kUniqueActivesExpectedParams.metric_report_ids.size(),
            CopyLocalAggregateStore().by_report_key().size());
  // Provide the EventAggregator with
  // |kNoiseFreeUniqueActivesMetricDefinitions|.
  auto noise_free_unique_actives_project_context =
      MakeProjectContext(kNoiseFreeUniqueActivesMetricDefinitions);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(
                     *noise_free_unique_actives_project_context));
  // Check that the number of key-value pairs in the LocalAggregateStore is
  // now equal to the number of distinct MetricReportIds of locally
  // aggregated reports in |kUniqueActivesMetricDefinitions| and
  // |kNoiseFreeUniqueActivesMetricDefinitions|.
  auto local_aggregate_store = CopyLocalAggregateStore();
  EXPECT_EQ(4u, local_aggregate_store.by_report_key().size());
  // The MetricReportId |kFeaturesActiveMetricReportId| appears in both
  // |kUniqueActivesMetricDefinitions| and
  // |kNoiseFreeUniqueActivesMetricDefinitions|. The associated
  // ReportAggregationKeys are identical, but the AggregationConfigs are
  // different.
  //
  // Check that the AggregationConfig stored in the LocalAggregateStore
  // under the key associated to |kFeaturesActiveMetricReportId| is the
  // first AggregationConfig that was provided for that key; i.e., is
  // derived from |kUniqueActivesMetricDefinitions|.
  std::string key;
  EXPECT_TRUE(
      SerializeToBase64(MakeAggregationKey(*unique_actives_project_context,
                                           kFeaturesActiveMetricReportId),
                        &key));
  auto unique_actives_config = MakeAggregationConfig(
      *unique_actives_project_context, kFeaturesActiveMetricReportId);
  auto report_aggregates = local_aggregate_store.by_report_key().find(key);
  EXPECT_NE(local_aggregate_store.by_report_key().end(), report_aggregates);
  EXPECT_TRUE(MessageDifferencer::Equals(
      unique_actives_config, report_aggregates->second.aggregation_config()));
  auto noise_free_config =
      MakeAggregationConfig(*noise_free_unique_actives_project_context,
                            kFeaturesActiveMetricReportId);
  EXPECT_FALSE(MessageDifferencer::Equals(
      noise_free_config, report_aggregates->second.aggregation_config()));
}

// Tests that EventAggregator::Log*Event returns |kInvalidArguments| when
// passed a report ID which is not associated to a key of the
// LocalAggregateStore, or when passed an EventRecord containing an Event
// proto message which is not of the appropriate event type.
TEST_F(EventAggregatorTest, LogBadEvents) {
  // Provide the EventAggregator with |kMetricDefinitions|.
  auto project_context = MakeProjectContext(kUniqueActivesMetricDefinitions);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(*project_context));
  // Attempt to log events for |kEventsOccurredMetricReportId|, which is not
  // in |kMetricDefinitions|. Check that the result is |kInvalidArguments|.
  auto noise_free_project_context =
      MakeProjectContext(kNoiseFreeUniqueActivesMetricDefinitions);
  EventRecord bad_event_record;
  bad_event_record.metric = noise_free_project_context->GetMetric(
      kEventsOccurredMetricReportId.first);
  bad_event_record.event->set_day_index(CurrentDayIndex());
  bad_event_record.event->mutable_occurrence_event();
  EXPECT_EQ(kInvalidArguments,
            event_aggregator_->LogUniqueActivesEvent(
                kEventsOccurredMetricReportId.second, &bad_event_record));
  bad_event_record.event->mutable_count_event();
  EXPECT_EQ(kInvalidArguments,
            event_aggregator_->LogPerDeviceCountEvent(
                kEventsOccurredMetricReportId.second, &bad_event_record));
  // Attempt to call LogUniqueActivesEvent() with a valid metric and report
  // ID, but with an EventRecord wrapping an Event which is not an
  // OccurrenceEvent. Check that the result is |kInvalidArguments|.
  bad_event_record.metric =
      project_context->GetMetric(kErrorsOccurredMetricReportId.first);
  bad_event_record.event->mutable_count_event();
  EXPECT_EQ(kInvalidArguments,
            event_aggregator_->LogUniqueActivesEvent(
                kErrorsOccurredMetricReportId.second, &bad_event_record));
  // Attempt to call LogPerDeviceCountEvent() with a valid metric and report
  // ID, but with an EventRecord wrapping an Event which is not a
  // CountEvent. Check that the result is |kInvalidArguments|.
  bad_event_record.metric =
      project_context->GetMetric(kConnectionFailuresMetricReportId.first);
  bad_event_record.event->mutable_occurrence_event();
  EXPECT_EQ(kInvalidArguments,
            event_aggregator_->LogPerDeviceCountEvent(
                kConnectionFailuresMetricReportId.second, &bad_event_record));
}

// Tests that EventAggregator::GenerateObservations() returns a positive
// status and that the expected number of Observations is generated when no
// Events have been logged to the EventAggregator.
TEST_F(EventAggregatorTest, GenerateObservationsNoEvents) {
  // Provide the EventAggregator with |kMetricDefinitions|.
  auto project_context = MakeProjectContext(kMetricDefinitions);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(*project_context));
  // Generate locally aggregated Observations for the current day index.
  EXPECT_EQ(kOK, GenerateObservations(CurrentDayIndex()));
  std::vector<Observation2> observations(0);
  EXPECT_TRUE(FetchAggregatedObservations(&observations, kExpectedParams,
                                          observation_store_.get(),
                                          update_recipient_.get()));
}

// Tests that EventAggregator::GenerateObservations() only generates
// Observations the first time it is called for a given day index.
TEST_F(EventAggregatorTest, GenerateObservationsTwice) {
  // Provide the EventAggregator with |kMetricDefinitions|.
  auto project_context = MakeProjectContext(kMetricDefinitions);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(*project_context));
  // Check that Observations are generated when GenerateObservations is called
  // for the current day index for the first time.
  auto current_day_index = CurrentDayIndex();
  EXPECT_EQ(kOK, GenerateObservations(current_day_index));
  std::vector<Observation2> observations(0);
  EXPECT_TRUE(FetchAggregatedObservations(&observations, kExpectedParams,
                                          observation_store_.get(),
                                          update_recipient_.get()));
  // Check that no Observations are generated when GenerateObservations is
  // called for the currentday index for the second time.
  ResetObservationStore();
  EXPECT_EQ(kOK, GenerateObservations(current_day_index));
  EXPECT_EQ(0u, observation_store_->messages_received.size());
}

// Tests that the LocalAggregateStore is updated as expected when
// EventAggregator::LogUniqueActivesEvent() is called with valid arguments;
// i.e., with a report ID associated to an existing key of the
// LocalAggregateStore, and with an EventRecord which wraps an
// OccurrenceEvent.
//
// Logs some valid events each day for 35 days, checking the contents of the
// LocalAggregateStore each day.
TEST_F(UniqueActivesEventAggregatorTest, LogEvents) {
  LoggedActivity logged_activity;
  uint32_t num_days = 35;
  for (uint32_t offset = 0; offset < num_days; offset++) {
    // Log an event for the FeaturesActive_UniqueDevices report of
    // |kUniqueActivesMetricDefinitions| with event code 0. Check the
    // contents of the LocalAggregateStore.
    auto day_index = CurrentDayIndex();
    EXPECT_EQ(kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId,
                                         day_index, 0u, &logged_activity));
    EXPECT_TRUE(CheckUniqueActivesAggregates(logged_activity, day_index));
    // Log another event for the same report, event code, and day index.
    // Check the contents of the LocalAggregateStore.
    EXPECT_EQ(kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId,
                                         day_index, 0u, &logged_activity));
    EXPECT_TRUE(CheckUniqueActivesAggregates(logged_activity, day_index));
    // Log several more events for various valid reports and event codes.
    // Check the contents of the LocalAggregateStore.
    EXPECT_EQ(kOK, LogUniqueActivesEvent(kDeviceBootsMetricReportId, day_index,
                                         0u, &logged_activity));
    EXPECT_EQ(kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId,
                                         day_index, 4u, &logged_activity));
    EXPECT_EQ(kOK, LogUniqueActivesEvent(kErrorsOccurredMetricReportId,
                                         day_index, 1u, &logged_activity));
    EXPECT_TRUE(CheckUniqueActivesAggregates(logged_activity, day_index));
    AdvanceClock(kDay);
  }
}

// Tests GarbageCollect() for UniqueActivesReportAggregates.
//
// For each value of N in the range [0, 34], logs some UniqueActivesEvents
// each day for N consecutive days and then garbage-collects the
// LocalAggregateStore. After garbage collection, verifies the contents of
// the LocalAggregateStore.
TEST_F(UniqueActivesEventAggregatorTest, GarbageCollect) {
  uint32_t max_days_before_gc = 35;
  for (uint32_t days_before_gc = 0; days_before_gc < max_days_before_gc;
       days_before_gc++) {
    SetUp();
    day_last_garbage_collected_ = 0u;
    LoggedActivity logged_activity;
    for (uint32_t offset = 0; offset < days_before_gc; offset++) {
      auto day_index = CurrentDayIndex();
      for (const auto& metric_report_id :
           kUniqueActivesExpectedParams.metric_report_ids) {
        // Log 2 events with event code 0.
        EXPECT_EQ(kOK, LogUniqueActivesEvent(metric_report_id, day_index, 0u,
                                             &logged_activity));
        EXPECT_EQ(kOK, LogUniqueActivesEvent(metric_report_id, day_index, 0u,
                                             &logged_activity));
        if (offset < 3) {
          // Log 1 event with event code 1.
          EXPECT_EQ(kOK, LogUniqueActivesEvent(metric_report_id, day_index, 1u,
                                               &logged_activity));
        }
      }
      AdvanceClock(kDay);
    }
    auto end_day_index = CurrentDayIndex();
    EXPECT_EQ(kOK, GarbageCollect(end_day_index));
    day_last_garbage_collected_ = end_day_index;
    EXPECT_TRUE(CheckUniqueActivesAggregates(logged_activity, end_day_index));
    TearDown();
  }
}

// Tests that EventAggregator::GenerateObservations() returns a positive
// status and that the expected number of Observations is generated when no
// Events have been logged to the EventAggregator.
TEST_F(UniqueActivesEventAggregatorTest, GenerateObservationsNoEvents) {
  EXPECT_EQ(kOK, GenerateObservations(CurrentDayIndex()));
  std::vector<Observation2> observations(0);
  EXPECT_TRUE(FetchAggregatedObservations(
      &observations, kUniqueActivesExpectedParams, observation_store_.get(),
      update_recipient_.get()));
}

// Tests that EventAggregator::GenerateObservations() only generates
// Observations the first time it is called for a given day index.
TEST_F(UniqueActivesEventAggregatorTest, GenerateObservationsTwice) {
  // Check that Observations are generated when GenerateObservations is called
  // for the current day index for the first time.
  auto current_day_index = CurrentDayIndex();
  EXPECT_EQ(kOK, GenerateObservations(current_day_index));
  std::vector<Observation2> observations(0);
  EXPECT_TRUE(FetchAggregatedObservations(
      &observations, kUniqueActivesExpectedParams, observation_store_.get(),
      update_recipient_.get()));
  // Check that no Observations are generated when GenerateObservations is
  // called for the currentday index for the second time.
  ResetObservationStore();
  EXPECT_EQ(kOK, GenerateObservations(current_day_index));
  EXPECT_EQ(0u, observation_store_->num_observations_added());
}

// Tests that EventAggregator::GenerateObservations() returns a positive
// status and that the expected number of Observations is generated after
// some UniqueActivesEvents have been logged, without any garbage
// collection.
//
// For 35 days, logs 2 events each day for the ErrorsOccurred_UniqueDevices
// report and 2 events for the FeaturesActive_UniqueDevices report, all
// with event code 0.
//
// Each day, calls GenerateObservations() with the day index of the previous
// day. Checks that a positive status is returned and that the
// FakeObservationStore has received the expected number of new observations
// for each locally aggregated report ID in |kUniqueActivesMetricDefinitions|.
TEST_F(UniqueActivesEventAggregatorTest, GenerateObservations) {
  int num_days = 35;
  std::vector<Observation2> observations(0);
  for (int offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    observations.clear();
    ResetObservationStore();
    EXPECT_EQ(kOK, GenerateObservations(day_index - 1));
    EXPECT_TRUE(FetchAggregatedObservations(
        &observations, kUniqueActivesExpectedParams, observation_store_.get(),
        update_recipient_.get()));
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogUniqueActivesEvent(kErrorsOccurredMetricReportId,
                                           day_index, 0u));
      EXPECT_EQ(kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId,
                                           day_index, 0u));
    }
    AdvanceClock(kDay);
  }
  observations.clear();
  ResetObservationStore();
  EXPECT_EQ(kOK, GenerateObservations(CurrentDayIndex() - 1));
  EXPECT_TRUE(FetchAggregatedObservations(
      &observations, kUniqueActivesExpectedParams, observation_store_.get(),
      update_recipient_.get()));
}

// Tests that GenerateObservations() returns a positive status and that the
// expected number of Observations is generated each day when Events are
// logged for UNIQUE_N_DAY_ACTIVES reports over multiple days, and when the
// LocalAggregateStore is garbage-collected each day.
//
// For 35 days, logs 2 events each day for the ErrorsOccurred_UniqueDevices
// report and 2 events for the FeaturesActive_UniqueDevices report, all
// with event code 0.
//
// Each day following the first day, calls GenerateObservations() and then
// GarbageCollect() with the day index of the current day. Checks that
// positive statuses are returned and that the FakeObservationStore has
// received the expected number of new observations for each locally
// aggregated report ID in |kUniqueActivesMetricDefinitions|.
TEST_F(UniqueActivesEventAggregatorTest, GenerateObservationsWithGc) {
  int num_days = 35;
  std::vector<Observation2> observations(0);
  for (int offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    observations.clear();
    ResetObservationStore();
    EXPECT_EQ(kOK, GenerateObservations(day_index - 1));
    EXPECT_TRUE(FetchAggregatedObservations(
        &observations, kUniqueActivesExpectedParams, observation_store_.get(),
        update_recipient_.get()));
    EXPECT_EQ(kOK, GarbageCollect(day_index));
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogUniqueActivesEvent(kErrorsOccurredMetricReportId,
                                           day_index, 0u));
      EXPECT_EQ(kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId,
                                           day_index, 0u));
    }
    AdvanceClock(kDay);
  }
  observations.clear();
  ResetObservationStore();
  auto day_index = CurrentDayIndex();
  EXPECT_EQ(kOK, GenerateObservations(day_index - 1));
  EXPECT_TRUE(FetchAggregatedObservations(
      &observations, kUniqueActivesExpectedParams, observation_store_.get(),
      update_recipient_.get()));
  EXPECT_EQ(kOK, GarbageCollect(day_index));
}

// Tests that GenerateObservations() returns a positive status and that the
// expected number of Observations is generated when events are logged over
// multiple days and some of those days' Observations are backfilled, without
// any garbage collection of the LocalAggregateStore.
//
// Sets the |backfill_days_| field of the EventAggregator to 3.
//
// Logging pattern:
// For 35 days, logs 2 events each day for the
// SomeErrorsOccurred_UniqueDevices report and 2 events for the
// SomeFeaturesActive_Unique_Devices report, all with event code 0.
//
// Observation generation pattern:
// Calls GenerateObservations() on the 1st through 5th and the 7th out of
// every 10 days, for 35 days.
//
// Expected numbers of Observations:
// It is expected that 4 days' worth of Observations are generated on
// the first day of every 10 (the day index for which GenerateObservations()
// was called, plus 3 days of backfill), that 1 day's worth of Observations
// are generated on the 2nd through 5th day of every 10, that 2 days'
// worth of Observations are generated on the 7th day of every 10 (the
// day index for which GenerateObservations() was called, plus 1 day of
// backfill), and that no Observations are generated on the remaining days.
TEST_F(UniqueActivesEventAggregatorTest, GenerateObservationsWithBackfill) {
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Log 2 events each day for 35 days. Call GenerateObservations() on the
  // first 5 day indices, and the 7th, out of every 10.
  for (int offset = 0; offset < 35; offset++) {
    auto day_index = CurrentDayIndex();
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogUniqueActivesEvent(kErrorsOccurredMetricReportId,
                                           day_index, 0u));
      EXPECT_EQ(kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId,
                                           day_index, 0u));
    }
    observation_store_->ResetObservationCounter();
    if (offset % 10 < 5 || offset % 10 == 6) {
      EXPECT_EQ(kOK, GenerateObservations(day_index));
    }
    auto num_new_obs = observation_store_->num_observations_added();
    EXPECT_GE(num_new_obs, 0u);
    // Check that the expected daily number of Observations was generated.
    switch (offset % 10) {
      case 0:
        EXPECT_EQ(
            kUniqueActivesExpectedParams.daily_num_obs * (backfill_days + 1),
            num_new_obs);
        break;
      case 1:
      case 2:
      case 3:
      case 4:
        EXPECT_EQ(kUniqueActivesExpectedParams.daily_num_obs, num_new_obs);
        break;
      case 6:
        EXPECT_EQ(kUniqueActivesExpectedParams.daily_num_obs * 2, num_new_obs);
        break;
      default:
        EXPECT_EQ(0u, num_new_obs);
    }
    AdvanceClock(kDay);
  }
}

// Tests that GenerateObservations() returns a positive status and that the
// expected number of Observations is generated when events are logged over
// multiple days and some of those days' Observations are backfilled, and when
// the LocalAggregateStore is garbage-collected after each call to
// GenerateObservations().
//
// Sets the |backfill_days_| field of the EventAggregator to 3.
//
// Logging pattern:
// For 35 days, logs 2 events each day for the
// SomeErrorsOccurred_UniqueDevices report and 2 events for the
// SomeFeaturesActive_Unique_Devices report, all with event code 0.
//
// Observation generation pattern:
// Calls GenerateObservations() on the 1st through 5th and the 7th out of
// every 10 days, for 35 days. Garbage-collects the LocalAggregateStore after
// each call.
//
// Expected numbers of Observations:
// It is expected that 4 days' worth of Observations are generated on
// the first day of every 10 (the day index for which GenerateObservations()
// was called, plus 3 days of backfill), that 1 day's worth of Observations
// are generated on the 2nd through 5th day of every 10, that 2 days'
// worth of Observations are generated on the 7th day of every 10 (the
// day index for which GenerateObservations() was called, plus 1 day of
// backfill), and that no Observations are generated on the remaining days.
TEST_F(UniqueActivesEventAggregatorTest,
       GenerateObservationsWithBackfillAndGc) {
  int num_days = 35;
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Log 2 events each day for 35 days. Call GenerateObservations() on the
  // first 5 day indices, and the 7th, out of every 10.
  for (int offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogUniqueActivesEvent(kErrorsOccurredMetricReportId,
                                           day_index, 0u));
      EXPECT_EQ(kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId,
                                           day_index, 0u));
    }
    observation_store_->ResetObservationCounter();
    if (offset % 10 < 5 || offset % 10 == 6) {
      EXPECT_EQ(kOK, GenerateObservations(day_index));
      EXPECT_EQ(kOK, GarbageCollect(day_index));
    }
    auto num_new_obs = observation_store_->num_observations_added();
    EXPECT_GE(num_new_obs, 0u);
    // Check that the expected daily number of Observations was generated.
    // This expected number is some multiple of the daily_num_obs field of
    // |kUniqueActivesExpectedParams|, depending on the number of days which
    // should have been backfilled when GenerateObservations() was called.
    switch (offset % 10) {
      case 0:
        EXPECT_EQ(
            kUniqueActivesExpectedParams.daily_num_obs * (backfill_days + 1),
            num_new_obs);
        break;
      case 1:
      case 2:
      case 3:
      case 4:
        EXPECT_EQ(kUniqueActivesExpectedParams.daily_num_obs, num_new_obs);
        break;
      case 6:
        EXPECT_EQ(kUniqueActivesExpectedParams.daily_num_obs * 2, num_new_obs);
        break;
      default:
        EXPECT_EQ(0u, num_new_obs);
    }
    AdvanceClock(kDay);
  }
}

// Checks that UniqueActivesObservations with the expected values (i.e.,
// non-active for all UNIQUE_N_DAY_ACTIVES reports, for all window sizes and
// event codes) are generated when no Events have been logged to the
// EventAggregator.
TEST_F(NoiseFreeUniqueActivesEventAggregatorTest,
       CheckObservationValuesNoEvents) {
  auto current_day_index = CurrentDayIndex();
  EXPECT_EQ(kOK, GenerateObservations(current_day_index));
  auto expected_obs = MakeNullExpectedUniqueActivesObservations(
      kNoiseFreeUniqueActivesExpectedParams, current_day_index);
  EXPECT_TRUE(CheckUniqueActivesObservations(
      expected_obs, observation_store_.get(), update_recipient_.get()));
}

// Checks that UniqueActivesObservations with the expected values are
// generated when GenerateObservations() is called for a single day index
// after logging some events for UNIQUE_N_DAY_ACTIVES reports for that day
// index, without any garbage collection or backfill.
//
// Logging pattern:
// Logs 2 occurrences of event code 0 for the FeaturesActives_UniqueDevices
// report, and 1 occurrence of event code 1 for the
// EventsOccurred_UniqueDevices report, all on the same day.
//
// Observation generation pattern:
// Calls GenerateObservations() after logging all events.
//
// Expected numbers of Observations:
// The expected number of Observations is the daily_num_obs field of
// |kNoiseFreeUniqueActivesExpectedParams|.
//
// Expected Observation values:
// All Observations should be labeled with the day index on which the events
// were logged.
//
// For the FeaturesActive_UniqueDevices report, expect activity indicators:
//
// window size        active for event codes
// ------------------------------------------
// 1                           0
// 7                           0
// 30                          0
//
// For the EventsOccurred_UniqueDevices report, expected activity indicators:
// window size        active for event codes
// ------------------------------------------
// 1                           1
// 7                           1
//
// All other Observations should be of inactivity.
TEST_F(NoiseFreeUniqueActivesEventAggregatorTest,
       CheckObservationValuesSingleDay) {
  auto day_index = CurrentDayIndex();
  // Log several events on |day_index|.
  EXPECT_EQ(
      kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId, day_index, 0u));
  EXPECT_EQ(
      kOK, LogUniqueActivesEvent(kFeaturesActiveMetricReportId, day_index, 0u));
  EXPECT_EQ(
      kOK, LogUniqueActivesEvent(kEventsOccurredMetricReportId, day_index, 1u));
  // Generate locally aggregated Observations for |day_index|.
  EXPECT_EQ(kOK, GenerateObservations(day_index));

  // Form the expected observations.
  auto expected_obs = MakeNullExpectedUniqueActivesObservations(
      kNoiseFreeUniqueActivesExpectedParams, day_index);
  expected_obs[{kFeaturesActiveMetricReportId, day_index}] = {
      {1, {true, false, false, false, false}},
      {7, {true, false, false, false, false}},
      {30, {true, false, false, false, false}}};
  expected_obs[{kEventsOccurredMetricReportId, day_index}] = {
      {1, {false, true, false, false, false}},
      {7, {false, true, false, false, false}}};

  // Check the contents of the FakeObservationStore.
  EXPECT_TRUE(CheckUniqueActivesObservations(
      expected_obs, observation_store_.get(), update_recipient_.get()));
}

// Checks that UniqueActivesObservations with the expected values are
// generated when some events have been logged for a UNIQUE_N_DAY_ACTIVES
// report for over multiple days and GenerateObservations() is called each
// day, without garbage collection or backfill.
//
// Logging pattern:
// Logs events for the SomeEventsOccurred_UniqueDevices report (whose parent
// metric has max_event_code = 4) for 10 days, according to the following
// pattern:
//
// * Never log event code 0.
// * On the i-th day (0-indexed) of logging, log an event for event code k,
// 1 <= k < 5, if 3*k divides i.
//
// Observation generation pattern:
// Each day following the first day, generates Observations for the previous
// day index.
//
// Expected number of Observations:
// Each call to GenerateObservations should generate a number of Observations
// equal to the daily_num_obs field of
// |kNoisefreeUniqueActivesExpectedParams|.
//
// Expected Observation values:
// The SomeEventsOccurred_UniqueDevices report has window sizes 1 and 7, and
// the expected activity indicators of Observations for that report on the
// i-th day are:
//
// (i, window size)            active for event codes
// ------------------------------------------------------
// (0, 1)                           1, 2, 3, 4
// (0, 7)                           1, 2, 3, 4
// (1, 1)                          ---
// (1, 7)                           1, 2, 3, 4
// (2, 1)                          ---
// (2, 7)                           1, 2, 3, 4
// (3, 1)                           1
// (3, 7)                           1, 2, 3, 4
// (4, 1)                          ---
// (4, 7)                           1, 2, 3, 4
// (5, 1)                          ---
// (5, 7)                           1, 2, 3, 4
// (6, 1)                           1, 2
// (6, 7)                           1, 2, 3, 4
// (7, 1)                          ---
// (7, 7)                           1, 2
// (8, 1)                          ---
// (8, 7)                           1, 2
// (9, 1)                           1, 3
// (9, 7)                           1, 2, 3
//
// All Observations for all other locally aggregated reports should be
// observations of non-occurrence.
TEST_F(NoiseFreeUniqueActivesEventAggregatorTest,
       CheckObservationValuesMultiDay) {
  auto start_day_index = CurrentDayIndex();
  // Form expected Obsevations for the 10 days of logging.
  uint32_t num_days = 10;
  std::vector<ExpectedUniqueActivesObservations> expected_obs(num_days);
  for (uint32_t offset = 0; offset < num_days; offset++) {
    expected_obs[offset] = MakeNullExpectedUniqueActivesObservations(
        kNoiseFreeUniqueActivesExpectedParams, start_day_index + offset);
  }
  expected_obs[0][{kEventsOccurredMetricReportId, start_day_index}] = {
      {1, {false, true, true, true, true}},
      {7, {false, true, true, true, true}}};
  expected_obs[1][{kEventsOccurredMetricReportId, start_day_index + 1}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[2][{kEventsOccurredMetricReportId, start_day_index + 2}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[3][{kEventsOccurredMetricReportId, start_day_index + 3}] = {
      {1, {false, true, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[4][{kEventsOccurredMetricReportId, start_day_index + 4}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[5][{kEventsOccurredMetricReportId, start_day_index + 5}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[6][{kEventsOccurredMetricReportId, start_day_index + 6}] = {
      {1, {false, true, true, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[7][{kEventsOccurredMetricReportId, start_day_index + 7}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, false, false}}};
  expected_obs[8][{kEventsOccurredMetricReportId, start_day_index + 8}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, false, false}}};
  expected_obs[9][{kEventsOccurredMetricReportId, start_day_index + 9}] = {
      {1, {false, true, false, true, false}},
      {7, {false, true, true, true, false}}};

  for (uint32_t offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    for (uint32_t event_code = 1;
         event_code < kNoiseFreeUniqueActivesExpectedParams.num_event_codes.at(
                          kEventsOccurredMetricReportId);
         event_code++) {
      if (offset % (3 * event_code) == 0) {
        EXPECT_EQ(kOK, LogUniqueActivesEvent(kEventsOccurredMetricReportId,
                                             day_index, event_code));
      }
    }
    // Clear the FakeObservationStore.
    ResetObservationStore();
    // Generate locally aggregated Observations.
    EXPECT_EQ(kOK, GenerateObservations(day_index));
    // Check the generated Observations against the expectation.
    EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs[offset],
                                               observation_store_.get(),
                                               update_recipient_.get()));
    AdvanceClock(kDay);
  }
}

// Checks that UniqueActivesObservations with the expected values are
// generated when some events have been logged for a UNIQUE_N_DAY_ACTIVES
// report for over multiple days and GenerateObservations() is called each
// day, and when the LocalAggregateStore is garbage-collected after each call
// to GenerateObservations().
//
// Logging pattern:
// Logs events for the SomeEventsOccurred_UniqueDevices report (whose parent
// metric has max_event_code = 4) for 10 days, according to the following
// pattern:
//
// * Never log event code 0.
// * On the i-th day (0-indexed) of logging, log an event for event code k,
// 1 <= k < 5, if 3*k divides i.
//
// Observation generation pattern:
// Each day following the first day, generates Observations for the previous
// day index.
//
// Expected number of Observations:
// Each call to GenerateObservations should generate a number of Observations
// equal to the daily_num_obs field of
// |kNoisefreeUniqueActivesExpectedParams|.
//
// Expected Observation values:
// The SomeEventsOccurred_UniqueDevices report has window sizes 1 and 7, and
// the expected activity indicators of Observations for that report on the
// i-th day are:
//
// (i, window size)            active for event codes
// ------------------------------------------------------
// (0, 1)                           1, 2, 3, 4
// (0, 7)                           1, 2, 3, 4
// (1, 1)                          ---
// (1, 7)                           1, 2, 3, 4
// (2, 1)                          ---
// (2, 7)                           1, 2, 3, 4
// (3, 1)                           1
// (3, 7)                           1, 2, 3, 4
// (4, 1)                          ---
// (4, 7)                           1, 2, 3, 4
// (5, 1)                          ---
// (5, 7)                           1, 2, 3, 4
// (6, 1)                           1, 2
// (6, 7)                           1, 2, 3, 4
// (7, 1)                          ---
// (7, 7)                           1, 2
// (8, 1)                          ---
// (8, 7)                           1, 2
// (9, 1)                           1, 3
// (9, 7)                           1, 2, 3
//
// All Observations for all other locally aggregated reports should be
// observations of non-occurrence.
TEST_F(NoiseFreeUniqueActivesEventAggregatorTest,
       CheckObservationValuesMultiDayWithGarbageCollection) {
  auto start_day_index = CurrentDayIndex();
  // Form expected Observations for the 10 days of logging.
  uint32_t num_days = 10;
  std::vector<ExpectedUniqueActivesObservations> expected_obs(num_days);
  for (uint32_t offset = 0; offset < num_days; offset++) {
    expected_obs[offset] = MakeNullExpectedUniqueActivesObservations(
        kNoiseFreeUniqueActivesExpectedParams, start_day_index + offset);
  }
  expected_obs[0][{kEventsOccurredMetricReportId, start_day_index}] = {
      {1, {false, true, true, true, true}},
      {7, {false, true, true, true, true}}};
  expected_obs[1][{kEventsOccurredMetricReportId, start_day_index + 1}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[2][{kEventsOccurredMetricReportId, start_day_index + 2}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[3][{kEventsOccurredMetricReportId, start_day_index + 3}] = {
      {1, {false, true, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[4][{kEventsOccurredMetricReportId, start_day_index + 4}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[5][{kEventsOccurredMetricReportId, start_day_index + 5}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[6][{kEventsOccurredMetricReportId, start_day_index + 6}] = {
      {1, {false, true, true, false, false}},
      {7, {false, true, true, true, true}}};
  expected_obs[7][{kEventsOccurredMetricReportId, start_day_index + 7}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, false, false}}};
  expected_obs[8][{kEventsOccurredMetricReportId, start_day_index + 8}] = {
      {1, {false, false, false, false, false}},
      {7, {false, true, true, false, false}}};
  expected_obs[9][{kEventsOccurredMetricReportId, start_day_index + 9}] = {
      {1, {false, true, false, true, false}},
      {7, {false, true, true, true, false}}};

  for (uint32_t offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    for (uint32_t event_code = 1;
         event_code < kNoiseFreeUniqueActivesExpectedParams.num_event_codes.at(
                          kEventsOccurredMetricReportId);
         event_code++) {
      if (offset % (3 * event_code) == 0) {
        EXPECT_EQ(kOK, LogUniqueActivesEvent(kEventsOccurredMetricReportId,
                                             day_index, event_code));
      }
    }
    // Advance |mock_clock_| by 1 day.
    AdvanceClock(kDay);
    // Clear the FakeObservationStore.
    ResetObservationStore();
    // Generate locally aggregated Observations and garbage-collect the
    // LocalAggregateStore, both for the previous day as measured by
    // |mock_clock_|. Back up the LocalAggregateStore and
    // AggregatedObservationHistoryStore.
    DoScheduledTasksNow();
    // Check the generated Observations against the expectation.
    EXPECT_TRUE(CheckUniqueActivesObservations(expected_obs[offset],
                                               observation_store_.get(),
                                               update_recipient_.get()));
  }
}

// Tests that the expected UniqueActivesObservations are generated when events
// are logged over multiple days and when Observations are backfilled for some
// days during that period, without any garbage-collection of the
// LocalAggregateStore.
//
// The test sets the number of backfill days to 3.
//
// Logging pattern:
// Events for the EventsOccurred_UniqueDevices report are logged over the days
// |start_day_index| to |start_day_index + 8| according to the following
// pattern:
//
// * For i = 0 to i = 4, log an event with event code i on day
// |start_day_index + i| and |start_day_index + 2*i|.
//
// Observation generation pattern:
// The test calls GenerateObservations() on day |start_day_index + i| for i =
// 0 through i = 5 and for i = 8, skipping the days |start_day_index + 6| and
// |start_day_index + 7|.
//
// Expected numbers of Observations:
// It is expected that 4 days' worth of Observations are generated on the
// first day (the day index for which GenerateObservations() was called, plus
// 3 days of backfill), that 1 day's worth of Observations is generated on the
// 2nd through 6th day, that 3 days' worth of Observations are generated on
// the 9th day (the day index for which GenerateObservations() was called,
// plus 2 days of backfill), and that no Observations are generated on the
// remaining days.
//
// Expected Observation values:
// The expected activity indicators of Observations for the
// EventsOccurred_UniqueDevices report for the i-th day of logging are:
//
// (i, window size)           active for event codes
// -------------------------------------------------------------------------
// (0, 1)                           0
// (0, 7)                           0
// (1, 1)                           1
// (1, 7)                           0, 1
// (2, 1)                           1, 2
// (2, 7)                           0, 1, 2
// (3, 1)                           3
// (3, 7)                           0, 1, 2, 3
// (4, 1)                           2, 4
// (4, 7)                           0, 1, 2, 3, 4
// (5, 1)                          ---
// (5, 7)                           0, 1, 2, 3, 4
// (6, 1)                           3
// (6, 7)                           0, 1, 2, 3, 4
// (7, 1)                          ---
// (7, 7)                           1, 2, 3, 4
// (8, 1)                           4
// (8, 7)                           1, 2, 3, 4
//
// All other Observations should be of non-activity.
TEST_F(NoiseFreeUniqueActivesEventAggregatorTest,
       CheckObservationValuesWithBackfill) {
  auto start_day_index = CurrentDayIndex();
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Log events for 9 days. Call GenerateObservations() on the first 6 day
  // indices, and the 9th.
  for (uint32_t offset = 0; offset < 9; offset++) {
    auto day_index = CurrentDayIndex();
    ResetObservationStore();
    for (uint32_t event_code = 0;
         event_code < kNoiseFreeUniqueActivesExpectedParams.num_event_codes.at(
                          kEventsOccurredMetricReportId);
         event_code++) {
      if (event_code == offset || (2 * event_code) == offset) {
        EXPECT_EQ(kOK, LogUniqueActivesEvent(kEventsOccurredMetricReportId,
                                             day_index, event_code));
      }
    }
    if (offset < 6 || offset == 8) {
      EXPECT_EQ(kOK, GenerateObservations(day_index));
    }
    // Make the set of Observations which are expected to be generated on
    // |start_day_index + offset| and check it against the contents of the
    // FakeObservationStore.
    ExpectedUniqueActivesObservations expected_obs;
    switch (offset) {
      case 0: {
        for (uint32_t day_index = start_day_index - backfill_days;
             day_index <= start_day_index; day_index++) {
          for (const auto& pair : MakeNullExpectedUniqueActivesObservations(
                   kNoiseFreeUniqueActivesExpectedParams, day_index)) {
            expected_obs.insert(pair);
          }
        }
        expected_obs[{kEventsOccurredMetricReportId, start_day_index}] = {
            {1, {true, false, false, false, false}},
            {7, {true, false, false, false, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 1: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, start_day_index + 1);
        expected_obs[{kEventsOccurredMetricReportId, start_day_index + 1}] = {
            {1, {false, true, false, false, false}},
            {7, {true, true, false, false, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 2: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, start_day_index + 2);
        expected_obs[{kEventsOccurredMetricReportId, start_day_index + 2}] = {
            {1, {false, true, true, false, false}},
            {7, {true, true, true, false, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 3: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, start_day_index + 3);
        expected_obs[{kEventsOccurredMetricReportId, start_day_index + 3}] = {
            {1, {false, false, false, true, false}},
            {7, {true, true, true, true, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 4: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, start_day_index + 4);
        expected_obs[{kEventsOccurredMetricReportId, start_day_index + 4}] = {
            {1, {false, false, true, false, true}},
            {7, {true, true, true, true, true}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 5: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, start_day_index + 5);
        expected_obs[{kEventsOccurredMetricReportId, start_day_index + 5}] = {
            {1, {false, false, false, false, false}},
            {7, {true, true, true, true, true}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 8: {
        for (uint32_t day_index = start_day_index + 6;
             day_index <= start_day_index + 8; day_index++) {
          for (const auto& pair : MakeNullExpectedUniqueActivesObservations(
                   kNoiseFreeUniqueActivesExpectedParams, day_index)) {
            expected_obs.insert(pair);
          }
        }
        expected_obs[{kEventsOccurredMetricReportId, start_day_index + 6}] = {
            {1, {false, false, false, true, false}},
            {7, {true, true, true, true, true}}};
        expected_obs[{kEventsOccurredMetricReportId, start_day_index + 7}] = {
            {1, {false, false, false, false, false}},
            {7, {false, true, true, true, true}}};
        expected_obs[{kEventsOccurredMetricReportId, start_day_index + 8}] = {
            {1, {false, false, false, false, true}},
            {7, {false, true, true, true, true}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      default:
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
    }
    AdvanceClock(kDay);
  }
}

// Tests that the expected UniqueActivesObservations are generated when events
// are logged over multiple days and when Observations are backfilled for some
// days during that period, and when the LocalAggregateStore is
// garbage-collected after each call to GenerateObservations().
//
// The test sets the number of backfill days to 3.
//
// Logging pattern:
// Events for the EventsOccurred_UniqueDevices report are logged over the days
// |start_day_index| to |start_day_index + 8| according to the following
// pattern:
//
// * For i = 0 to i = 4, log an event with event code i on day
// |start_day_index + i| and |start_day_index + 2*i|.
//
// Observation generation pattern:
// The test calls GenerateObservations() on day |start_day_index + i| for i =
// 0 through i = 5 and for i = 8, skipping the days |start_day_index + 6| and
// |start_day_index + 7|.
//
// Expected numbers of Observations:
// It is expected that 4 days' worth of Observations are generated on the
// first day (the day index for which GenerateObservations() was called, plus
// 3 days of backfill), that 1 day's worth of Observations is generated on the
// 2nd through 6th day, that 3 days' worth of Observations are generated on
// the 9th day (the day index for which GenerateObservations() was called,
// plus 2 days of backfill), and that no Observations are generated on the
// remaining days.
//
// Expected Observation values:
// The expected activity indicators of Observations for the
// EventsOccurred_UniqueDevices report for the i-th day of logging are:
//
// (i, window size)           active for event codes
// -------------------------------------------------------------------------
// (0, 1)                           0
// (0, 7)                           0
// (1, 1)                           1
// (1, 7)                           0, 1
// (2, 1)                           1, 2
// (2, 7)                           0, 1, 2
// (3, 1)                           3
// (3, 7)                           0, 1, 2, 3
// (4, 1)                           2, 4
// (4, 7)                           0, 1, 2, 3, 4
// (5, 1)                          ---
// (5, 7)                           0, 1, 2, 3, 4
// (6, 1)                           3
// (6, 7)                           0, 1, 2, 3, 4
// (7, 1)                          ---
// (7, 7)                           1, 2, 3, 4
// (8, 1)                           4
// (8, 7)                           1, 2, 3, 4
//
// All other Observations should be of non-activity.
TEST_F(NoiseFreeUniqueActivesEventAggregatorTest,
       CheckObservationValuesWithBackfillAndGc) {
  auto start_day_index = CurrentDayIndex();
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Log events for 9 days. Call GenerateObservations() on the first 6 day
  // indices, and the 9th.
  for (uint32_t offset = 0; offset < 8; offset++) {
    auto day_index = CurrentDayIndex();
    ResetObservationStore();
    for (uint32_t event_code = 0;
         event_code < kNoiseFreeUniqueActivesExpectedParams.num_event_codes.at(
                          kEventsOccurredMetricReportId);
         event_code++) {
      if (event_code == offset || (2 * event_code) == offset) {
        EXPECT_EQ(kOK, LogUniqueActivesEvent(kEventsOccurredMetricReportId,
                                             day_index, event_code));
      }
    }
    // Advance |mock_clock_| by 1 day.
    AdvanceClock(kDay);
    if (offset < 6 || offset == 9) {
      // Generate Observations and garbage-collect, both for the previous day
      // index according to |mock_clock_|. Back up the LocalAggregateStore and
      // the AggregatedObservationHistoryStore.
      DoScheduledTasksNow();
    }
    // Make the set of Observations which are expected to be generated on
    // |start_day_index + offset| and check it against the contents of the
    // FakeObservationStore.
    ExpectedUniqueActivesObservations expected_obs;
    switch (offset) {
      case 0: {
        for (uint32_t day_index = start_day_index - backfill_days;
             day_index <= start_day_index; day_index++) {
          for (const auto& pair : MakeNullExpectedUniqueActivesObservations(
                   kNoiseFreeUniqueActivesExpectedParams, day_index)) {
            expected_obs.insert(pair);
          }
        }
        expected_obs[{kEventsOccurredMetricReportId, start_day_index}] = {
            {1, {true, false, false, false, false}},
            {7, {true, false, false, false, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 1: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, start_day_index + 1);
        expected_obs[{kEventsOccurredMetricReportId, start_day_index + 1}] = {
            {1, {false, true, false, false, false}},
            {7, {true, true, false, false, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 2: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, start_day_index + 2);
        expected_obs[{kEventsOccurredMetricReportId, start_day_index + 2}] = {
            {1, {false, true, true, false, false}},
            {7, {true, true, true, false, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 3: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, start_day_index + 3);
        expected_obs[{kEventsOccurredMetricReportId, start_day_index + 3}] = {
            {1, {false, false, false, true, false}},
            {7, {true, true, true, true, false}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 4: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, start_day_index + 4);
        expected_obs[{kEventsOccurredMetricReportId, start_day_index + 4}] = {
            {1, {false, false, true, false, true}},
            {7, {true, true, true, true, true}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 5: {
        expected_obs = MakeNullExpectedUniqueActivesObservations(
            kNoiseFreeUniqueActivesExpectedParams, start_day_index + 5);
        expected_obs[{kEventsOccurredMetricReportId, start_day_index + 5}] = {
            {1, {false, false, false, false, false}},
            {7, {true, true, true, true, true}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 8: {
        for (uint32_t day_index = start_day_index + 6;
             day_index <= start_day_index + 8; day_index++) {
          for (const auto& pair : MakeNullExpectedUniqueActivesObservations(
                   kNoiseFreeUniqueActivesExpectedParams, day_index)) {
            expected_obs.insert(pair);
          }
        }
        expected_obs[{kEventsOccurredMetricReportId, start_day_index + 6}] = {
            {1, {false, false, false, true, false}},
            {7, {true, true, true, true, true}}};
        expected_obs[{kEventsOccurredMetricReportId, start_day_index + 7}] = {
            {1, {false, false, false, false, false}},
            {7, {false, true, true, true, true}}};
        expected_obs[{kEventsOccurredMetricReportId, start_day_index + 8}] = {
            {1, {false, false, false, false, true}},
            {7, {false, true, true, true, true}}};
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
        break;
      }
      default:
        EXPECT_TRUE(CheckUniqueActivesObservations(
            expected_obs, observation_store_.get(), update_recipient_.get()));
    }
  }
}

// Tests that the LocalAggregateStore is updated as expected when
// EventAggregator::LogPerDeviceCountEvent() is called with valid arguments;
// i.e., with a report ID associated to an existing key of the
// LocalAggregateStore, and with an EventRecord which wraps a CountEvent.
//
// Logs some valid events each day for 35 days, checking the contents of the
// LocalAggregateStore each day.
TEST_F(PerDeviceCountEventAggregatorTest, LogEvents) {
  LoggedCounts logged_counts;
  uint32_t num_days = 35;
  for (uint32_t offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    EXPECT_EQ(kOK, LogPerDeviceCountEvent(kConnectionFailuresMetricReportId,
                                          day_index, "component_A", 0u, 5,
                                          &logged_counts));
    EXPECT_EQ(kOK, LogPerDeviceCountEvent(kConnectionFailuresMetricReportId,
                                          day_index, "component_A", 0u, 7,
                                          &logged_counts));
    EXPECT_EQ(kOK, LogPerDeviceCountEvent(kConnectionFailuresMetricReportId,
                                          day_index, "component_A", 1u, 3,
                                          &logged_counts));
    EXPECT_EQ(kOK,
              LogPerDeviceCountEvent(kSettingsChangedMetricReportId, day_index,
                                     "component_B", 0u, 10, &logged_counts));
    EXPECT_EQ(kOK,
              LogPerDeviceCountEvent(kSettingsChangedMetricReportId, day_index,
                                     "component_A", 0u, 2, &logged_counts));
    EXPECT_EQ(kOK, LogPerDeviceCountEvent(kConnectionFailuresMetricReportId,
                                          day_index, "component_C", 0u, 15,
                                          &logged_counts));
    EXPECT_EQ(kOK,
              LogPerDeviceCountEvent(kSettingsChangedMetricReportId, day_index,
                                     "component_B", 0u, 4, &logged_counts));
    EXPECT_TRUE(CheckPerDeviceCountAggregates(logged_counts, day_index));
    AdvanceClock(kDay);
  }
}

// Tests GarbageCollect() for PerDeviceCountReportAggregates.
//
// For each value of N in the range [0, 34], logs some CountEvents for a
// PerDeviceCount report each day for N consecutive days, and then
// garbage-collects the LocalAggregateStore. After garbage collection, verifies
// the contents of the LocalAggregateStore.
TEST_F(PerDeviceCountEventAggregatorTest, GarbageCollect) {
  uint32_t max_days_before_gc = 35;
  for (uint32_t days_before_gc = 0; days_before_gc < max_days_before_gc;
       days_before_gc++) {
    SetUp();
    day_last_garbage_collected_ = 0u;
    LoggedCounts logged_counts;
    for (uint32_t offset = 0; offset < days_before_gc; offset++) {
      auto day_index = CurrentDayIndex();
      for (const auto& metric_report_id :
           kPerDeviceCountExpectedParams.metric_report_ids) {
        for (const auto& component :
             {"component_A", "component_B", "component_C"}) {
          // Log 2 events with event code 0, for each component A, B, C.
          EXPECT_EQ(kOK,
                    LogPerDeviceCountEvent(metric_report_id, day_index,
                                           component, 0u, 2, &logged_counts));
          EXPECT_EQ(kOK,
                    LogPerDeviceCountEvent(metric_report_id, day_index,
                                           component, 0u, 3, &logged_counts));
        }
        if (offset < 3) {
          // Log 1 event for component D and event code 1.
          EXPECT_EQ(kOK, LogPerDeviceCountEvent(metric_report_id, day_index,
                                                "component_D", 1u, 4,
                                                &logged_counts));
        }
      }
      AdvanceClock(kDay);
    }
    auto end_day_index = CurrentDayIndex();
    EXPECT_EQ(kOK, GarbageCollect(end_day_index));
    day_last_garbage_collected_ = end_day_index;
    EXPECT_TRUE(CheckPerDeviceCountAggregates(logged_counts, end_day_index));
    TearDown();
  }
}

// Tests that EventAggregator::GenerateObservations() returns a positive
// status and that the expected number of Observations is generated after
// some CountEvents have been logged for PerDeviceCount reports, without any
// garbage collection.
//
// For 35 days, logs a positive number of events each day for the
// ConnectionFailures_PerDeviceCount report with "component_A" and for
// the SettingsChanged_PerDeviceCount report with "component_B", all with event
// code 0.
//
// Each day, calls GenerateObservations() with the day index of the previous
// day. Checks that a positive status is returned and that the
// FakeObservationStore has received the expected number of new observations
// for each locally aggregated report ID in |kPerDeviceCountMetricDefinitions|.
TEST_F(PerDeviceCountEventAggregatorTest, GenerateObservations) {
  int num_days = 1;
  std::vector<Observation2> observations(0);
  ExpectedAggregationParams expected_params = kPerDeviceCountExpectedParams;
  for (int offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    observations.clear();
    ResetObservationStore();
    EXPECT_EQ(kOK, GenerateObservations(day_index - 1));
    EXPECT_TRUE(FetchAggregatedObservations(&observations, expected_params,
                                            observation_store_.get(),
                                            update_recipient_.get()));
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogPerDeviceCountEvent(kConnectionFailuresMetricReportId,
                                            day_index, "component_A", 0u, 1));
      EXPECT_EQ(kOK, LogPerDeviceCountEvent(kSettingsChangedMetricReportId,
                                            day_index, "component_B", 0u, 5));
    }
    // If this is the first time we're logging events, update the expected
    // numbers of generated Observations to account for the logged events.
    // For each report, for each window size, expect 1 Observation more than if
    // no events had been logged.
    if (offset == 0) {
      expected_params.daily_num_obs += 3;
      expected_params.num_obs_per_report[kConnectionFailuresMetricReportId] +=
          1;
      expected_params.num_obs_per_report[kSettingsChangedMetricReportId] += 2;
    }
    AdvanceClock(kDay);
  }
  observations.clear();
  ResetObservationStore();
  EXPECT_EQ(kOK, GenerateObservations(CurrentDayIndex() - 1));
  EXPECT_TRUE(FetchAggregatedObservations(&observations, expected_params,
                                          observation_store_.get(),
                                          update_recipient_.get()));
}

// Tests that EventAggregator::GenerateObservations() returns a positive
// status and that the expected number of Observations is generated after
// some CountEvents have been logged for PerDeviceCount reports over multiple
// days, and when the LocalAggregateStore is garbage-collected each day.
//
// For 35 days, logs a positive number of events each day for the
// ConnectionFailures_PerDeviceCount report with "component_A" and for
// the SettingsChanged_PerDeviceCount report with "component_B", all with event
// code 0.
//
// Each day, calls GenerateObservations() with the day index of the previous
// day. Checks that a positive status is returned and that the
// FakeObservationStore has received the expected number of new observations
// for each locally aggregated report ID in |kPerDeviceCountMetricDefinitions|.
TEST_F(PerDeviceCountEventAggregatorTest, GenerateObservationsWithGc) {
  int num_days = 35;
  std::vector<Observation2> observations(0);
  ExpectedAggregationParams expected_params = kPerDeviceCountExpectedParams;
  for (int offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    observations.clear();
    ResetObservationStore();
    EXPECT_EQ(kOK, GenerateObservations(day_index - 1));
    EXPECT_TRUE(FetchAggregatedObservations(&observations, expected_params,
                                            observation_store_.get(),
                                            update_recipient_.get()));
    EXPECT_EQ(kOK, GarbageCollect(day_index));
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogPerDeviceCountEvent(kConnectionFailuresMetricReportId,
                                            day_index, "component_A", 0u, 1));
      EXPECT_EQ(kOK, LogPerDeviceCountEvent(kSettingsChangedMetricReportId,
                                            day_index, "component_B", 0u, 5));
    }
    // If this is the first time we're logging events, update the expected
    // numbers of generated Observations to account for the logged events.
    // For each report, for each window size, expect 1 Observation more than if
    // no events had been logged.
    if (offset == 0) {
      expected_params.daily_num_obs += 3;
      expected_params.num_obs_per_report[kConnectionFailuresMetricReportId] +=
          1;
      expected_params.num_obs_per_report[kSettingsChangedMetricReportId] += 2;
    }
    AdvanceClock(kDay);
  }
  observations.clear();
  ResetObservationStore();
  auto day_index = CurrentDayIndex();
  EXPECT_EQ(kOK, GenerateObservations(day_index - 1));
  EXPECT_TRUE(FetchAggregatedObservations(&observations, expected_params,
                                          observation_store_.get(),
                                          update_recipient_.get()));
  EXPECT_EQ(kOK, GarbageCollect(day_index));
}

// Tests that GenerateObservations() returns a positive status and that the
// expected number of Observations is generated when events are logged over
// multiple days and some of those days' Observations are backfilled, without
// any garbage collection of the LocalAggregateStore.
//
// Sets the |backfill_days_| field of the EventAggregator to 3.
//
// Logging pattern:
// For 35 days, logs 2 events each day for the
// SomeErrorsOccurred_UniqueDevices report and 2 events for the
// SomeFeaturesActive_Unique_Devices report, all with event code 0.
//
// Observation generation pattern:
// Calls GenerateObservations() on the 1st through 5th and the 7th out of
// every 10 days, for 35 days.
//
// Expected numbers of Observations:
// It is expected that 4 days' worth of Observations are generated on
// the first day of every 10 (the day index for which GenerateObservations()
// was called, plus 3 days of backfill), that 1 day's worth of Observations
// are generated on the 2nd through 5th day of every 10, that 2 days'
// worth of Observations are generated on the 7th day of every 10 (the
// day index for which GenerateObservations() was called, plus 1 day of
// backfill), and that no Observations are generated on the remaining days.
TEST_F(PerDeviceCountEventAggregatorTest, GenerateObservationsWithBackfill) {
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Log 2 events each day for 35 days. Call GenerateObservations() on the
  // first 5 day indices, and the 7th, out of every 10.
  for (int offset = 0; offset < 35; offset++) {
    auto day_index = CurrentDayIndex();
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogPerDeviceCountEvent(kConnectionFailuresMetricReportId,
                                            day_index, "component_A", 0u, 1));
      EXPECT_EQ(kOK, LogPerDeviceCountEvent(kSettingsChangedMetricReportId,
                                            day_index, "component_B", 0u, 5));
    }
    auto num_obs_before = observation_store_->messages_received.size();
    if (offset % 10 < 5 || offset % 10 == 6) {
      EXPECT_EQ(kOK, GenerateObservations(day_index));
    }
    auto num_obs_after = observation_store_->messages_received.size();
    EXPECT_GE(num_obs_after, num_obs_before);
    // Check that the expected daily number of Observations was generated.
    switch (offset % 10) {
      case 0:
        // If this is the first day of logging, expect 3 Observations for each
        // day in the backfill period and 6 Observations for the current day.
        if (offset == 0) {
          EXPECT_EQ(
              (kPerDeviceCountExpectedParams.daily_num_obs * backfill_days) +
                  kPerDeviceCountExpectedParams.daily_num_obs + 3,
              num_obs_after - num_obs_before);
        } else {
          // If this is another day whose offset is a multiple of 10, expect 6
          // Observations for each day in the backfill period as well as the
          // current day.
          EXPECT_EQ((kPerDeviceCountExpectedParams.daily_num_obs + 3) *
                        (backfill_days + 1),
                    num_obs_after - num_obs_before);
        }
        break;
      case 1:
      case 2:
      case 3:
      case 4:
        // Expect 6 Observations for this day.
        EXPECT_EQ(kPerDeviceCountExpectedParams.daily_num_obs + 3,
                  num_obs_after - num_obs_before);
        break;
      case 6:
        // Expect 6 Observations for each of today and yesterday.
        EXPECT_EQ((kPerDeviceCountExpectedParams.daily_num_obs + 3) * 2,
                  num_obs_after - num_obs_before);
        break;
      default:
        EXPECT_EQ(num_obs_after, num_obs_before);
    }
    AdvanceClock(kDay);
  }
}

// Tests that GenerateObservations() returns a positive status and that the
// expected number of Observations is generated when events are logged over
// multiple days and some of those days' Observations are backfilled, and when
// the LocalAggregateStore is garbage-collected after each call to
// GenerateObservations().
//
// Sets the |backfill_days_| field of the EventAggregator to 3.
//
// Logging pattern:
// For 35 days, logs 2 events each day for the
// ConnectionFailures_PerDeviceCount report with "component_A" and 2 events for
// the SettingsChanged_PerDeviceCount report with "component_B", all with event
// code 0.
//
// Observation generation pattern:
// Calls GenerateObservations() on the 1st through 5th and the 7th out of
// every 10 days, for 35 days. Garbage-collects the LocalAggregateStore after
// each call.
//
// Expected numbers of Observations:
// It is expected that 4 days' worth of Observations are generated on
// the first day of every 10 (the day index for which GenerateObservations()
// was called, plus 3 days of backfill), that 1 day's worth of Observations
// are generated on the 2nd through 5th day of every 10, that 2 days'
// worth of Observations are generated on the 7th day of every 10 (the
// day index for which GenerateObservations() was called, plus 1 day of
// backfill), and that no Observations are generated on the remaining days.
TEST_F(PerDeviceCountEventAggregatorTest,
       GenerateObservationsWithBackfillAndGc) {
  int num_days = 35;
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Log 2 events each day for 35 days. Call GenerateObservations() on the
  // first 5 day indices, and the 7th, out of every 10.
  for (int offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(kOK, LogPerDeviceCountEvent(kConnectionFailuresMetricReportId,
                                            day_index, "component_A", 0u, 1));
      EXPECT_EQ(kOK, LogPerDeviceCountEvent(kSettingsChangedMetricReportId,
                                            day_index, "component_B", 0u, 5));
    }
    auto num_obs_before = observation_store_->messages_received.size();
    if (offset % 10 < 5 || offset % 10 == 6) {
      EXPECT_EQ(kOK, GenerateObservations(day_index));
      EXPECT_EQ(kOK, GarbageCollect(day_index));
    }
    auto num_obs_after = observation_store_->messages_received.size();
    EXPECT_GE(num_obs_after, num_obs_before);
    // Check that the expected daily number of Observations was generated.
    switch (offset % 10) {
      case 0:
        // If this is the first day of logging, expect 3 Observations for each
        // day in the backfill period and 6 Observations for the current day.
        if (offset == 0) {
          EXPECT_EQ(
              (kPerDeviceCountExpectedParams.daily_num_obs * backfill_days) +
                  kPerDeviceCountExpectedParams.daily_num_obs + 3,
              num_obs_after - num_obs_before);
        } else {
          // If this is another day whose offset is a multiple of 10, expect 6
          // Observations for each day in the backfill period as well as the
          // current day.
          EXPECT_EQ((kPerDeviceCountExpectedParams.daily_num_obs + 3) *
                        (backfill_days + 1),
                    num_obs_after - num_obs_before);
        }
        break;
      case 1:
      case 2:
      case 3:
      case 4:
        // Expect 6 Observations for this day.
        EXPECT_EQ(kPerDeviceCountExpectedParams.daily_num_obs + 3,
                  num_obs_after - num_obs_before);
        break;
      case 6:
        // Expect 6 Observations for each of today and yesterday.
        EXPECT_EQ((kPerDeviceCountExpectedParams.daily_num_obs + 3) * 2,
                  num_obs_after - num_obs_before);
        break;
      default:
        EXPECT_EQ(num_obs_after, num_obs_before);
    }
    AdvanceClock(kDay);
  }
}

// Generate Observations without logging any events, and check that the
// resulting Observations are as expected: 1 ReportParticipationObservation for
// each PER_DEVICE_COUNT_STATS report in the config, and no
// PerDeviceCountObservations.
TEST_F(PerDeviceCountEventAggregatorTest, CheckObservationValuesNoEvents) {
  auto current_day_index = CurrentDayIndex();
  EXPECT_EQ(kOK, GenerateObservations(current_day_index));
  auto expected_report_participation_obs =
      MakeExpectedReportParticipationObservations(kPerDeviceCountExpectedParams,
                                                  current_day_index);
  EXPECT_TRUE(CheckPerDeviceCountObservations(
      {}, expected_report_participation_obs, observation_store_.get(),
      update_recipient_.get()));
}

// Check that the expected PerDeviceCountObservations and
// ReportParticipationObservations are generated when GenerateObservations() is
// called after logging some events for PER_DEVICE_COUNT_STATS reports over a
// single day index.
TEST_F(PerDeviceCountEventAggregatorTest, CheckObservationValuesSingleDay) {
  auto day_index = CurrentDayIndex();
  // Log several events on |day_index|.
  EXPECT_EQ(kOK, LogPerDeviceCountEvent(kConnectionFailuresMetricReportId,
                                        day_index, "component_A", 0u, 5));
  EXPECT_EQ(kOK, LogPerDeviceCountEvent(kConnectionFailuresMetricReportId,
                                        day_index, "component_B", 0u, 5));
  EXPECT_EQ(kOK, LogPerDeviceCountEvent(kConnectionFailuresMetricReportId,
                                        day_index, "component_A", 0u, 5));
  EXPECT_EQ(kOK, LogPerDeviceCountEvent(kConnectionFailuresMetricReportId,
                                        day_index, "component_A", 1u, 5));
  EXPECT_EQ(kOK, LogPerDeviceCountEvent(kSettingsChangedMetricReportId,
                                        day_index, "component_C", 0u, 5));
  EXPECT_EQ(kOK, LogPerDeviceCountEvent(kSettingsChangedMetricReportId,
                                        day_index, "component_C", 0u, 5));
  // Generate locally aggregated Observations for |day_index|.
  EXPECT_EQ(kOK, GenerateObservations(day_index));

  // Form the expected Observations.
  auto expected_report_participation_obs =
      MakeExpectedReportParticipationObservations(kPerDeviceCountExpectedParams,
                                                  day_index);
  ExpectedPerDeviceCountObservations expected_per_device_count_obs;
  expected_per_device_count_obs[{kConnectionFailuresMetricReportId,
                                 day_index}][1] = {
      {"component_A", 0u, 10}, {"component_A", 1u, 5}, {"component_B", 0u, 5}};
  expected_per_device_count_obs[{kSettingsChangedMetricReportId, day_index}]
                               [7] = {{"component_C", 0u, 10}};
  expected_per_device_count_obs[{kSettingsChangedMetricReportId, day_index}]
                               [30] = {{"component_C", 0u, 10}};
  EXPECT_TRUE(CheckPerDeviceCountObservations(
      expected_per_device_count_obs, expected_report_participation_obs,
      observation_store_.get(), update_recipient_.get()));
}

// Checks that PerDeviceCountObservations with the expected values are
// generated when some events have been logged for a PER_DEVICE_COUNT
// report for over multiple days and GenerateObservations() is called each
// day, without garbage collection or backfill.
//
// Logged events for the SettingsChanged_PerDeviceCount metric on the i-th day:
//
//  i            (component, event code, count)
// -----------------------------------------------------------------------
//  0
//  1          ("A", 1, 3)
//  2          ("A", 1, 3), ("A", 2, 3), ("B", 1, 2)
//  3          ("A", 1, 3)
//  4          ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
//  5          ("A", 1, 3)
//  6          ("A", 1, 3), ("A", 2, 3), ("B", 1, 2)
//  7          ("A", 1, 3)
//  8          ("A", 1, 3), ("A", 2, 3), ("B", 1, 2), ("B", 2, 2)
//  9          ("A", 1, 3)
//
// Expected PerDeviceCountObservations for the SettingsChanged_PerDeviceCount
// report on the i-th day:
//
// (i, window size)          (component, event code, count)
// -----------------------------------------------------------------------
// (0, 7)
// (0, 30)
// (1, 7)     ("A", 1,  3)
// (1, 30)    ("A", 1,  3)
// (2, 7)     ("A", 1,  6),  ("A", 2,  3), ("B", 1, 2)
// (2, 30)    ("A", 1,  6),  ("A", 2,  3), ("B", 1, 2)
// (3, 7)     ("A", 1,  9),  ("A", 2,  3), ("B", 1, 2)
// (3, 30)    ("A", 1,  9),  ("A", 2,  3), ("B", 1, 2)
// (4, 7)     ("A", 1, 12),  ("A", 2,  6), ("B", 1, 4), ("B", 2, 2)
// (4, 30)    ("A", 1, 12),  ("A", 2,  6), ("B", 1, 4), ("B", 2, 2)
// (5, 7)     ("A", 1, 15),  ("A", 2,  6), ("B", 1, 4), ("B", 2, 2)
// (5, 30)    ("A", 1, 15),  ("A", 2,  6), ("B", 1, 4), ("B", 2, 2)
// (6, 7)     ("A", 1, 18),  ("A", 2,  9), ("B", 1, 6), ("B", 2, 2)
// (6, 30)    ("A", 1, 18),  ("A", 2,  9), ("B", 1, 6), ("B", 2, 2)
// (7, 7)     ("A", 1, 21),  ("A", 2,  9), ("B", 1, 6), ("B", 2, 2)
// (7, 30)    ("A", 1, 21),  ("A", 2,  9), ("B", 1, 6), ("B", 2, 2)
// (8, 7)     ("A", 1, 21),  ("A", 2, 12), ("B", 1, 8), ("B", 2, 4)
// (8, 30)    ("A", 1, 24),  ("A", 2, 12), ("B", 1, 8), ("B", 2, 4)
// (9, 7)     ("A", 1, 21),  ("A", 2,  9), ("B", 1, 6), ("B", 2, 4)
// (9, 30)    ("A", 1, 27),  ("A", 2, 12), ("B", 1, 8), ("B", 2, 4)
//
// In addition, expect 2 ReportParticipationObservations each day, 1 for each
// of ConnectionFailures_PerDeviceCount and SettingsChanged_PerDeviceCount.
TEST_F(PerDeviceCountEventAggregatorTest, CheckObservationValuesMultiDay) {
  auto start_day_index = CurrentDayIndex();
  // Form expected Observations for the 10 days of logging.
  uint32_t num_days = 10;
  std::vector<ExpectedPerDeviceCountObservations> expected_per_device_count_obs(
      num_days);
  std::vector<ExpectedReportParticipationObservations>
      expected_report_participation_obs(num_days);
  for (uint32_t offset = 0; offset < num_days; offset++) {
    expected_report_participation_obs[offset] =
        MakeExpectedReportParticipationObservations(
            kPerDeviceCountExpectedParams, start_day_index + offset);
  }
  expected_per_device_count_obs[0] = {};
  expected_per_device_count_obs[1][{kSettingsChangedMetricReportId,
                                    start_day_index + 1}] = {
      {7, {{"A", 1u, 3}}}, {30, {{"A", 1u, 3}}}};
  expected_per_device_count_obs[2][{kSettingsChangedMetricReportId,
                                    start_day_index + 2}] = {
      {7, {{"A", 1u, 6}, {"A", 2u, 3}, {"B", 1u, 2}}},
      {30, {{"A", 1u, 6}, {"A", 2u, 3}, {"B", 1u, 2}}}};
  expected_per_device_count_obs[3][{kSettingsChangedMetricReportId,
                                    start_day_index + 3}] = {
      {7, {{"A", 1u, 9}, {"A", 2u, 3}, {"B", 1u, 2}}},
      {30, {{"A", 1u, 9}, {"A", 2u, 3}, {"B", 1u, 2}}}};
  expected_per_device_count_obs[4][{kSettingsChangedMetricReportId,
                                    start_day_index + 4}] = {
      {7, {{"A", 1u, 12}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}},
      {30, {{"A", 1u, 12}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}}};
  expected_per_device_count_obs[5][{kSettingsChangedMetricReportId,
                                    start_day_index + 5}] = {
      {7, {{"A", 1u, 15}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}},
      {30, {{"A", 1u, 15}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}}};
  expected_per_device_count_obs[6][{kSettingsChangedMetricReportId,
                                    start_day_index + 6}] = {
      {7, {{"A", 1u, 18}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}},
      {30, {{"A", 1u, 18}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}}};
  expected_per_device_count_obs[7][{kSettingsChangedMetricReportId,
                                    start_day_index + 7}] = {
      {7, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}},
      {30, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}}};
  expected_per_device_count_obs[8][{kSettingsChangedMetricReportId,
                                    start_day_index + 8}] = {
      {7, {{"A", 1u, 21}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}},
      {30, {{"A", 1u, 24}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}}};
  expected_per_device_count_obs[9][{kSettingsChangedMetricReportId,
                                    start_day_index + 9}] = {
      {7, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 4}}},
      {30, {{"A", 1u, 27}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}}};

  for (uint32_t offset = 0; offset < 1; offset++) {
    auto day_index = CurrentDayIndex();
    for (uint32_t event_code = 1; event_code < 3; event_code++) {
      if (offset > 0 && offset % event_code == 0) {
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(kSettingsChangedMetricReportId,
                                              day_index, "A", event_code, 3));
      }
      if (offset > 0 && offset % (2 * event_code) == 0) {
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(kSettingsChangedMetricReportId,
                                              day_index, "B", event_code, 2));
      }
    }
    // Clear the FakeObservationStore.
    ResetObservationStore();
    // Generate locally aggregated Observations.
    EXPECT_EQ(kOK, GenerateObservations(day_index));
    EXPECT_TRUE(CheckPerDeviceCountObservations(
        expected_per_device_count_obs[offset],
        expected_report_participation_obs[offset], observation_store_.get(),
        update_recipient_.get()))
        << "offset = " << offset;
    AdvanceClock(kDay);
  }
}

// Repeat the CheckObservationValuesMultiDay test, this time calling
// GarbageCollect() after each call to GenerateObservations.
//
// The logging pattern and set of Observations for each day index is the same
// as in PerDeviceCountEventAggregatorTest::CheckObservationValuesMultiDay. See
// that test for documentation.
TEST_F(PerDeviceCountEventAggregatorTest,
       CheckObservationValuesMultiDayWithGarbageCollection) {
  auto start_day_index = CurrentDayIndex();
  // Form expected Observations for the 10 days of logging.
  uint32_t num_days = 10;
  std::vector<ExpectedPerDeviceCountObservations> expected_per_device_count_obs(
      num_days);
  std::vector<ExpectedReportParticipationObservations>
      expected_report_participation_obs(num_days);
  for (uint32_t offset = 0; offset < num_days; offset++) {
    expected_report_participation_obs[offset] =
        MakeExpectedReportParticipationObservations(
            kPerDeviceCountExpectedParams, start_day_index + offset);
  }
  expected_per_device_count_obs[0] = {};
  expected_per_device_count_obs[1][{kSettingsChangedMetricReportId,
                                    start_day_index + 1}] = {
      {7, {{"A", 1u, 3}}}, {30, {{"A", 1u, 3}}}};
  expected_per_device_count_obs[2][{kSettingsChangedMetricReportId,
                                    start_day_index + 2}] = {
      {7, {{"A", 1u, 6}, {"A", 2u, 3}, {"B", 1u, 2}}},
      {30, {{"A", 1u, 6}, {"A", 2u, 3}, {"B", 1u, 2}}}};
  expected_per_device_count_obs[3][{kSettingsChangedMetricReportId,
                                    start_day_index + 3}] = {
      {7, {{"A", 1u, 9}, {"A", 2u, 3}, {"B", 1u, 2}}},
      {30, {{"A", 1u, 9}, {"A", 2u, 3}, {"B", 1u, 2}}}};
  expected_per_device_count_obs[4][{kSettingsChangedMetricReportId,
                                    start_day_index + 4}] = {
      {7, {{"A", 1u, 12}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}},
      {30, {{"A", 1u, 12}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}}};
  expected_per_device_count_obs[5][{kSettingsChangedMetricReportId,
                                    start_day_index + 5}] = {
      {7, {{"A", 1u, 15}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}},
      {30, {{"A", 1u, 15}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}}};
  expected_per_device_count_obs[6][{kSettingsChangedMetricReportId,
                                    start_day_index + 6}] = {
      {7, {{"A", 1u, 18}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}},
      {30, {{"A", 1u, 18}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}}};
  expected_per_device_count_obs[7][{kSettingsChangedMetricReportId,
                                    start_day_index + 7}] = {
      {7, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}},
      {30, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}}};
  expected_per_device_count_obs[8][{kSettingsChangedMetricReportId,
                                    start_day_index + 8}] = {
      {7, {{"A", 1u, 21}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}},
      {30, {{"A", 1u, 24}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}}};
  expected_per_device_count_obs[9][{kSettingsChangedMetricReportId,
                                    start_day_index + 9}] = {
      {7, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 4}}},
      {30, {{"A", 1u, 27}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}}};

  for (uint32_t offset = 0; offset < 10; offset++) {
    auto day_index = CurrentDayIndex();
    for (uint32_t event_code = 1; event_code < 3; event_code++) {
      if (offset > 0 && offset % event_code == 0) {
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(kSettingsChangedMetricReportId,
                                              day_index, "A", event_code, 3));
      }
      if (offset > 0 && offset % (2 * event_code) == 0) {
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(kSettingsChangedMetricReportId,
                                              day_index, "B", event_code, 2));
      }
    }
    // Advance |mock_clock_| by 1 day.
    AdvanceClock(kDay);
    // Clear the FakeObservationStore.
    ResetObservationStore();
    // Generate locally aggregated Observations and garbage-collect the
    // LocalAggregateStore, both for the previous day as measured by
    // |mock_clock_|. Back up the LocalAggregateStore and
    // AggregatedObservationHistoryStore.
    DoScheduledTasksNow();
    EXPECT_TRUE(CheckPerDeviceCountObservations(
        expected_per_device_count_obs[offset],
        expected_report_participation_obs[offset], observation_store_.get(),
        update_recipient_.get()));
  }
}

// Tests that the expected PerDeviceCountObservations are generated when events
// are logged over multiple days and when Observations are backfilled for some
// days during that period, without any garbage-collection of the
// LocalAggregateStore.
//
// The logging pattern and set of Observations for each day index is the same as
// in PerDeviceCountEventAggregatorTest::CheckObservationValuesMultiDay. See
// that test for documentation.
TEST_F(PerDeviceCountEventAggregatorTest, CheckObservationValuesWithBackfill) {
  auto start_day_index = CurrentDayIndex();
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Log events for 9 days. Call GenerateObservations() on the first 6 day
  // indices, and the 9th.
  uint32_t num_days = 9;
  for (uint32_t offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    ResetObservationStore();
    for (uint32_t event_code = 1; event_code < 3; event_code++) {
      if (offset > 0 && (offset % event_code == 0)) {
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(kSettingsChangedMetricReportId,
                                              day_index, "A", event_code, 3));
      }
      if (offset > 0 && offset % (2 * event_code) == 0) {
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(kSettingsChangedMetricReportId,
                                              day_index, "B", event_code, 2));
      }
    }
    if (offset < 6 || offset == 8) {
      EXPECT_EQ(kOK, GenerateObservations(day_index));
    }
    // Make the set of Observations which are expected to be generated on
    // |start_day_index + offset| and check it against the contents of the
    // FakeObservationStore.
    ExpectedPerDeviceCountObservations expected_per_device_count_obs;
    ExpectedReportParticipationObservations expected_report_participation_obs;
    switch (offset) {
      case 0: {
        for (uint32_t day_index = start_day_index - backfill_days;
             day_index <= start_day_index; day_index++) {
          for (const auto& pair : MakeExpectedReportParticipationObservations(
                   kPerDeviceCountExpectedParams, day_index)) {
            expected_report_participation_obs.insert(pair);
          }
        }
        EXPECT_TRUE(CheckPerDeviceCountObservations(
            expected_per_device_count_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 1: {
        expected_per_device_count_obs[{kSettingsChangedMetricReportId,
                                       day_index}] = {{7, {{"A", 1u, 3}}},
                                                      {30, {{"A", 1u, 3}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(
                kPerDeviceCountExpectedParams, day_index);
        EXPECT_TRUE(CheckPerDeviceCountObservations(
            expected_per_device_count_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 2: {
        expected_per_device_count_obs[{kSettingsChangedMetricReportId,
                                       day_index}] = {
            {7, {{"A", 1u, 6}, {"A", 2u, 3}, {"B", 1u, 2}}},
            {30, {{"A", 1u, 6}, {"A", 2u, 3}, {"B", 1u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(
                kPerDeviceCountExpectedParams, day_index);
        EXPECT_TRUE(CheckPerDeviceCountObservations(
            expected_per_device_count_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 3: {
        expected_per_device_count_obs[{kSettingsChangedMetricReportId,
                                       day_index}] = {
            {7, {{"A", 1u, 9}, {"A", 2u, 3}, {"B", 1u, 2}}},
            {30, {{"A", 1u, 9}, {"A", 2u, 3}, {"B", 1u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(
                kPerDeviceCountExpectedParams, day_index);
        EXPECT_TRUE(CheckPerDeviceCountObservations(
            expected_per_device_count_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 4: {
        expected_per_device_count_obs[{kSettingsChangedMetricReportId,
                                       day_index}] = {
            {7, {{"A", 1u, 12}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}},
            {30, {{"A", 1u, 12}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(
                kPerDeviceCountExpectedParams, day_index);
        EXPECT_TRUE(CheckPerDeviceCountObservations(
            expected_per_device_count_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 5: {
        expected_per_device_count_obs[{kSettingsChangedMetricReportId,
                                       day_index}] = {
            {7, {{"A", 1u, 15}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}},
            {30, {{"A", 1u, 15}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(
                kPerDeviceCountExpectedParams, day_index);
        EXPECT_TRUE(CheckPerDeviceCountObservations(
            expected_per_device_count_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 8: {
        expected_per_device_count_obs[{kSettingsChangedMetricReportId,
                                       start_day_index + 6}] = {
            {7, {{"A", 1u, 18}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}},
            {30, {{"A", 1u, 18}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}}};
        expected_per_device_count_obs[{kSettingsChangedMetricReportId,
                                       start_day_index + 7}] = {
            {7, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}},
            {30, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}}};
        expected_per_device_count_obs[{kSettingsChangedMetricReportId,
                                       start_day_index + 8}] = {
            {7, {{"A", 1u, 21}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}},
            {30, {{"A", 1u, 24}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}}};
        for (uint32_t day_index = start_day_index + 6;
             day_index <= start_day_index + 8; day_index++) {
          for (const auto& pair : MakeExpectedReportParticipationObservations(
                   kPerDeviceCountExpectedParams, day_index)) {
            expected_report_participation_obs.insert(pair);
          }
        }
        EXPECT_TRUE(CheckPerDeviceCountObservations(
            expected_per_device_count_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      default:
        EXPECT_TRUE(CheckPerDeviceCountObservations(
            expected_per_device_count_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
    }
    AdvanceClock(kDay);
  }
}

// Tests that the expected Observations are generated for PerDeviceCount reports
// when events are logged over multiple days and when Observations are
// backfilled for some days during that period, and when the
// LocalAggregatedStore is garbage-collected after each call to
// GenerateObservations().
//
// The logging pattern and set of Observations for each day index is the same as
// in PerDeviceCountEventAggregatorTest::CheckObservationValuesMultiDay. See
// that test for documentation.
TEST_F(PerDeviceCountEventAggregatorTest,
       CheckObservationValuesWithBackfillAndGc) {
  auto start_day_index = CurrentDayIndex();
  // Set |backfill_days_| to 3.
  size_t backfill_days = 3;
  SetBackfillDays(backfill_days);
  // Log events for 9 days. Call GenerateObservations() on the first 6 day
  // indices, and the 9th.
  uint32_t num_days = 9;
  for (uint32_t offset = 0; offset < num_days; offset++) {
    auto day_index = CurrentDayIndex();
    ResetObservationStore();
    for (uint32_t event_code = 1; event_code < 3; event_code++) {
      if (offset > 0 && (offset % event_code == 0)) {
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(kSettingsChangedMetricReportId,
                                              day_index, "A", event_code, 3));
      }
      if (offset > 0 && offset % (2 * event_code) == 0) {
        EXPECT_EQ(kOK, LogPerDeviceCountEvent(kSettingsChangedMetricReportId,
                                              day_index, "B", event_code, 2));
      }
    }
    // Advance |mock_clock_| by 1 day.
    AdvanceClock(kDay);
    if (offset < 6 || offset == 8) {
      // Generate Observations and garbage-collect, both for the previous day
      // index according to |mock_clock_|. Back up the LocalAggregateStore and
      // the AggregatedObservationHistoryStore.
      DoScheduledTasksNow();
    }
    // Make the set of Observations which are expected to be generated on
    // |start_day_index + offset| and check it against the contents of the
    // FakeObservationStore.
    ExpectedPerDeviceCountObservations expected_per_device_count_obs;
    ExpectedReportParticipationObservations expected_report_participation_obs;
    switch (offset) {
      case 0: {
        for (uint32_t day_index = start_day_index - backfill_days;
             day_index <= start_day_index; day_index++) {
          for (const auto& pair : MakeExpectedReportParticipationObservations(
                   kPerDeviceCountExpectedParams, day_index)) {
            expected_report_participation_obs.insert(pair);
          }
        }
        EXPECT_TRUE(CheckPerDeviceCountObservations(
            expected_per_device_count_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 1: {
        expected_per_device_count_obs[{kSettingsChangedMetricReportId,
                                       day_index}] = {{7, {{"A", 1u, 3}}},
                                                      {30, {{"A", 1u, 3}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(
                kPerDeviceCountExpectedParams, day_index);
        EXPECT_TRUE(CheckPerDeviceCountObservations(
            expected_per_device_count_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 2: {
        expected_per_device_count_obs[{kSettingsChangedMetricReportId,
                                       day_index}] = {
            {7, {{"A", 1u, 6}, {"A", 2u, 3}, {"B", 1u, 2}}},
            {30, {{"A", 1u, 6}, {"A", 2u, 3}, {"B", 1u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(
                kPerDeviceCountExpectedParams, day_index);
        EXPECT_TRUE(CheckPerDeviceCountObservations(
            expected_per_device_count_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 3: {
        expected_per_device_count_obs[{kSettingsChangedMetricReportId,
                                       day_index}] = {
            {7, {{"A", 1u, 9}, {"A", 2u, 3}, {"B", 1u, 2}}},
            {30, {{"A", 1u, 9}, {"A", 2u, 3}, {"B", 1u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(
                kPerDeviceCountExpectedParams, day_index);
        EXPECT_TRUE(CheckPerDeviceCountObservations(
            expected_per_device_count_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 4: {
        expected_per_device_count_obs[{kSettingsChangedMetricReportId,
                                       day_index}] = {
            {7, {{"A", 1u, 12}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}},
            {30, {{"A", 1u, 12}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(
                kPerDeviceCountExpectedParams, day_index);
        EXPECT_TRUE(CheckPerDeviceCountObservations(
            expected_per_device_count_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 5: {
        expected_per_device_count_obs[{kSettingsChangedMetricReportId,
                                       day_index}] = {
            {7, {{"A", 1u, 15}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}},
            {30, {{"A", 1u, 15}, {"A", 2u, 6}, {"B", 1u, 4}, {"B", 2u, 2}}}};
        expected_report_participation_obs =
            MakeExpectedReportParticipationObservations(
                kPerDeviceCountExpectedParams, day_index);
        EXPECT_TRUE(CheckPerDeviceCountObservations(
            expected_per_device_count_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      case 8: {
        expected_per_device_count_obs[{kSettingsChangedMetricReportId,
                                       start_day_index + 6}] = {
            {7, {{"A", 1u, 18}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}},
            {30, {{"A", 1u, 18}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}}};
        expected_per_device_count_obs[{kSettingsChangedMetricReportId,
                                       start_day_index + 7}] = {
            {7, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}},
            {30, {{"A", 1u, 21}, {"A", 2u, 9}, {"B", 1u, 6}, {"B", 2u, 2}}}};
        expected_per_device_count_obs[{kSettingsChangedMetricReportId,
                                       start_day_index + 8}] = {
            {7, {{"A", 1u, 21}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}},
            {30, {{"A", 1u, 24}, {"A", 2u, 12}, {"B", 1u, 8}, {"B", 2u, 4}}}};
        for (uint32_t day_index = start_day_index + 6;
             day_index <= start_day_index + 8; day_index++) {
          for (const auto& pair : MakeExpectedReportParticipationObservations(
                   kPerDeviceCountExpectedParams, day_index)) {
            expected_report_participation_obs.insert(pair);
          }
        }
        EXPECT_TRUE(CheckPerDeviceCountObservations(
            expected_per_device_count_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
        break;
      }
      default:
        EXPECT_TRUE(CheckPerDeviceCountObservations(
            expected_per_device_count_obs, expected_report_participation_obs,
            observation_store_.get(), update_recipient_.get()));
    }
  }
}

// Tests GenerateObservations() and GarbageCollect() in the case where the
// LocalAggregateStore contains aggregates for metrics with both UTC and LOCAL
// time zone policies, and where the day index in local time may be less than
// the day index in UTC.
TEST_F(NoiseFreeMixedTimeZoneEventAggregatorTest, LocalBeforeUTC) {
  std::vector<ExpectedUniqueActivesObservations> expected_obs(3);
  // Begin at a time when the current day index is the same in both UTC and
  // local time. Log 1 event for event code 0 for each of the 2 reports, then
  // generate Observations and garbage-collect for the previous day index in
  // each of UTC and local time.
  auto start_day_index = CurrentDayIndex();
  LogUniqueActivesEvent(kDeviceBootsMetricReportId, start_day_index, 0u);
  LogUniqueActivesEvent(kFeaturesActiveMetricReportId, start_day_index, 0u);
  GenerateObservations(start_day_index - 1, start_day_index - 1);
  GarbageCollect(start_day_index - 1, start_day_index - 1);
  // Form the expected contents of the FakeObservationStore.
  // Since no events were logged on the previous day and no Observations have
  // been generated for that day yet, expect Observations of non-activity for
  // all event codes, for both reports.
  expected_obs[0] = MakeNullExpectedUniqueActivesObservations(
      kNoiseFreeMixedTimeZoneExpectedParams, start_day_index - 1);
  EXPECT_TRUE(CheckUniqueActivesObservations(
      expected_obs[0], observation_store_.get(), update_recipient_.get()));
  ResetObservationStore();
  // Advance the day index in UTC, but not in local time, and log 1 event for
  // event code 1 for each of the 2 reports. Generate Observations and
  // garbage-collect for the previous day in each of UTC and local time.
  LogUniqueActivesEvent(kDeviceBootsMetricReportId, start_day_index, 1u);
  LogUniqueActivesEvent(kFeaturesActiveMetricReportId, start_day_index + 1, 1u);
  GenerateObservations(start_day_index, start_day_index - 1);
  GarbageCollect(start_day_index, start_day_index - 1);
  // Form the expected contents of the FakeObservationStore. Since
  // Observations have already been generated for the
  // DeviceBoots_UniqueDevices report for |start_day_index - 1|, expect no
  // Observations for that report.
  expected_obs[1][{kFeaturesActiveMetricReportId, start_day_index}] = {
      {1, {true, false, false}}};
  EXPECT_TRUE(CheckUniqueActivesObservations(
      expected_obs[1], observation_store_.get(), update_recipient_.get()));
  ResetObservationStore();
  // Advance the day index in local time so that it is equal to the day index
  // in UTC. Log 1 event for event code 2 for each of the 2 reports, then
  // generate Observations and garbage-collect for the previous day in each of
  // UTC and local time.
  LogUniqueActivesEvent(kDeviceBootsMetricReportId, start_day_index + 1, 2u);
  LogUniqueActivesEvent(kFeaturesActiveMetricReportId, start_day_index + 1, 2u);
  GenerateObservations(start_day_index, start_day_index);
  GarbageCollect(start_day_index, start_day_index);
  // Form the expected contents of the FakeObservationStore. Since
  // Observations have already been generated for the
  // FeaturesActive_UniqueDevices report for day |start_day_index|, expect no
  // Observations for that report.
  expected_obs[2][{kDeviceBootsMetricReportId, start_day_index}] = {
      {1, {true, true, false}}};
  EXPECT_TRUE(CheckUniqueActivesObservations(
      expected_obs[2], observation_store_.get(), update_recipient_.get()));
}

// Tests GenerateObservations() and GarbageCollect() in the case where the
// LocalAggregateStore contains aggregates for metrics with both UTC and LOCAL
// time zone policies, and where the day index in UTC may be less than
// the day index in local time.
TEST_F(NoiseFreeMixedTimeZoneEventAggregatorTest, LocalAfterUTC) {
  std::vector<ExpectedUniqueActivesObservations> expected_obs(3);
  // Begin at a time when the current day index is the same in both UTC and
  // local time. Log 1 event for event code 0 for each of the 2 reports, then
  // generate Observations and garbage-collect for the previous day index in
  // each of UTC and local time.
  auto start_day_index = CurrentDayIndex();
  LogUniqueActivesEvent(kDeviceBootsMetricReportId, start_day_index, 0u);
  LogUniqueActivesEvent(kFeaturesActiveMetricReportId, start_day_index, 0u);
  GenerateObservations(start_day_index - 1, start_day_index - 1);
  GarbageCollect(start_day_index - 1, start_day_index - 1);
  // Form the expected contents of the FakeObservationStore.
  // Since no events were logged on the previous day and no Observations have
  // been generated for that day yet, expect Observations of non-activity for
  // all event codes, for both reports.
  expected_obs[0] = MakeNullExpectedUniqueActivesObservations(
      kNoiseFreeMixedTimeZoneExpectedParams, start_day_index - 1);
  EXPECT_TRUE(CheckUniqueActivesObservations(
      expected_obs[0], observation_store_.get(), update_recipient_.get()));
  ResetObservationStore();
  // Advance the day index in local time, but not in UTC, and log 1 event for
  // event code 1 for each of the 2 reports. Generate Observations and
  // garbage-collect for the previous day in each of UTC and local time.
  LogUniqueActivesEvent(kDeviceBootsMetricReportId, start_day_index + 1, 1u);
  LogUniqueActivesEvent(kFeaturesActiveMetricReportId, start_day_index, 1u);
  GenerateObservations(start_day_index - 1, start_day_index);
  GarbageCollect(start_day_index - 1, start_day_index);
  // Form the expected contents of the FakeObservationStore. Since
  // Observations have already been generated for the
  // FeaturesActive_UniqueDevices report for |start_day_index - 1|, expect no
  // Observations for that report.
  expected_obs[1][{kDeviceBootsMetricReportId, start_day_index}] = {
      {1, {true, false, false}}};
  EXPECT_TRUE(CheckUniqueActivesObservations(
      expected_obs[1], observation_store_.get(), update_recipient_.get()));
  ResetObservationStore();
  // Advance the day index in UTC so that it is equal to the day index in
  // local time. Log 1 event for event code 2 for each of the 2 reports, then
  // generate Observations and garbage-collect for the previous day in each of
  // UTC and local time.
  LogUniqueActivesEvent(kDeviceBootsMetricReportId, start_day_index + 1, 2u);
  LogUniqueActivesEvent(kFeaturesActiveMetricReportId, start_day_index + 1, 2u);
  GenerateObservations(start_day_index, start_day_index);
  GarbageCollect(start_day_index, start_day_index);
  // Form the expected contents of the FakeObservationStore. Since
  // Observations have already been generated for the
  // DeviceBoots_UniqueDevices report for day |start_day_index|, expect no
  // Observations for that report.
  expected_obs[2][{kFeaturesActiveMetricReportId, start_day_index}] = {
      {1, {true, true, false}}};
  EXPECT_TRUE(CheckUniqueActivesObservations(
      expected_obs[2], observation_store_.get(), update_recipient_.get()));
}

// Starts the worker thread, and destructs the EventAggregator without
// explicitly shutting down the worker thread. Checks that the shutdown flag
// and worker thread are in the expected states before and after the thread is
// started.
TEST_F(EventAggregatorWorkerTest, StartWorkerThread) {
  EXPECT_TRUE(in_shutdown_state());
  event_aggregator_->Start();
  EXPECT_TRUE(in_run_state());
}

// Starts the worker thread, shuts down the worker thread, and destructs the
// EventAggregator. Checks that the shutdown flag and worker thread are in the
// expected states.
TEST_F(EventAggregatorWorkerTest, StartAndShutDownWorkerThread) {
  EXPECT_TRUE(in_shutdown_state());
  event_aggregator_->Start();
  EXPECT_TRUE(in_run_state());
  ShutDownWorkerThread();
  EXPECT_TRUE(in_shutdown_state());
}

// Starts the worker thread and immediately shuts it down. Checks that the
// LocalAggregateStore was backed up during shutdown.
TEST_F(EventAggregatorWorkerTest, BackUpBeforeShutdown) {
  event_aggregator_->Start();
  ShutDownWorkerThread();
  EXPECT_EQ(1, local_aggregate_proto_store_->write_count_);
}

// Starts the worker thread and calls
// EventAggregator::UpdateAggregationConfigs() on the main thread.
TEST_F(EventAggregatorWorkerTest, UpdateAggregationConfigs) {
  event_aggregator_->Start();
  // Provide the EventAggregator with |kMetricDefinitions|.
  auto project_context = MakeProjectContext(kMetricDefinitions);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(*project_context));
  // Check that the number of key-value pairs in the LocalAggregateStore is
  // now equal to the number of locally aggregated reports in
  // |kMetricDefinitions|.
  EXPECT_EQ(kExpectedParams.metric_report_ids.size(),
            CopyLocalAggregateStore().by_report_key().size());
}

// Starts the worker thread, provides a ProjectContext, logs some events, and
// shuts down the worker thread. Checks that the LocalAggregateStore was
// backed up at least once during the lifetime of the worker thread.
TEST_F(EventAggregatorWorkerTest, LogEvents) {
  auto day_index = CurrentDayIndex();
  event_aggregator_->Start();
  // Provide the EventAggregator with |kMetricDefinitions|.
  auto project_context = MakeProjectContext(kMetricDefinitions);
  EXPECT_EQ(kOK, event_aggregator_->UpdateAggregationConfigs(*project_context));
  // Log some events.
  LoggedActivity logged_activity;
  EXPECT_EQ(kOK, LogUniqueActivesEvent(*project_context,
                                       kErrorsOccurredMetricReportId, day_index,
                                       1u, &logged_activity));
  EXPECT_TRUE(CheckUniqueActivesAggregates(logged_activity, day_index));
  ShutDownWorkerThread();
  EXPECT_GE(local_aggregate_proto_store_->write_count_, 1);
}

}  // namespace logger
}  // namespace cobalt

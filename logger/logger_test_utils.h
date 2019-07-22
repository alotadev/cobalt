// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_LOGGER_LOGGER_TEST_UTILS_H_
#define COBALT_LOGGER_LOGGER_TEST_UTILS_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "./gtest.h"
#include "config/project_configs.h"
#include "encoder/shipping_manager.h"
#include "logger/encoder.h"
#include "logger/fake_logger.h"
#include "logger/local_aggregation.pb.h"
#include "logger/logger_interface.h"
#include "logger/project_context.h"
#include "util/consistent_proto_store.h"
#include "util/posix_file_system.h"
#include "util/status.h"

namespace cobalt {
namespace logger {
namespace testing {

// A container for information about the set of all locally aggregated reports
// in a registry. This is used by tests to check the output of the
// EventAggregator.
using ExpectedAggregationParams = struct ExpectedAggregationParams {
  // The total number of locally aggregated Observations which should be
  // generated for a single day, assuming that no events have been logged.
  size_t daily_num_obs = 0;
  // The MetricReportIds of the locally aggregated reports in the registry.
  std::set<MetricReportId> metric_report_ids;
  // Keys are the MetricReportIds of all locally aggregated reports in the
  // registry. The value at a key is the number of Observations which should be
  // generated each day for that report, assuming that no events have been
  // logged.
  std::map<MetricReportId, size_t> num_obs_per_report;
  // Keys are the MetricReportIds of all UNIQUE_N_DAY_ACTIVES reports in the
  // registry. The value at a key is the number of event codes for that report's
  // parent MetricDefinition.
  std::map<MetricReportId, size_t> num_event_codes;
  // Keys are the MetricReportIds of all locally aggregated reports in the
  // registry. The value at a key is the set of window sizes of that report.
  std::map<MetricReportId, std::set<uint32_t>> window_sizes;
};

// A representation of a set of expected UniqueActivesObservations. Used to
// check the values of UniqueActivesObservations generated by the
// EventAggregator.
//
// The outer map is keyed by pairs (MetricReportId, day_index), where the day
// index represents the day index of the expected Observation, and the value at
// a pair is a map keyed by window size. The value of the inner map at a window
// size is a vector of size equal to the number of event codes for the parent
// metric of the report, and the i-th element of the vector is |true| if the
// i-th event code occurred on the device during the specified window, or
// |false| if not.
using ExpectedUniqueActivesObservations =
    std::map<std::pair<MetricReportId, uint32_t>,
             std::map<uint32_t, std::vector<bool>>>;

// A representation of a set of expected PerDeviceNumericObservations. Used to
// check the values of PerDeviceNumericObservations generated by the
// EventAggregator.
//
// The outer map is keyed by pairs (MetricReportId, day_index), where the day
// index represents the day index of the expected Observation.
//
// The values of the inner map are tuples (component, packed event code, value).
using ExpectedPerDeviceNumericObservations = std::map<
    std::pair<MetricReportId, uint32_t>,
    std::map<uint32_t, std::set<std::tuple<std::string, uint64_t, int64_t>>>>;

// A representation of a set of expected ReportParticipationObservations. Used
// to check the values of ReportParticipationObservations generated by the
// EventAggregator. The first element of each pair is the MetricReportId of a
// report, and the second element represents the day index of an expected
// Observation for that report. a pair is a a set of window sizes.
using ExpectedReportParticipationObservations =
    std::set<std::pair<MetricReportId, uint32_t>>;

// A mock ObservationStore.
class FakeObservationStore
    : public ::cobalt::encoder::ObservationStoreWriterInterface {
 public:
  StoreStatus AddEncryptedObservation(
      std::unique_ptr<EncryptedMessage> message,
      std::unique_ptr<ObservationMetadata> metadata) override {
    messages_received.emplace_back(std::move(message));
    metadata_received.emplace_back(std::move(metadata));
    num_observations_added_++;
    return kOk;
  }

  std::vector<std::unique_ptr<EncryptedMessage>> messages_received;  // NOLINT
  std::vector<std::unique_ptr<ObservationMetadata>>
      metadata_received;  // NOLINT

  size_t num_observations_added() { return num_observations_added_; }

  void ResetObservationCounter() { num_observations_added_ = 0; }

 private:
  size_t num_observations_added_ = 0;
};

// A mock ObservationStoreUpdateRecipient.
class TestUpdateRecipient
    : public ::cobalt::encoder::ObservationStoreUpdateRecipient {
 public:
  void NotifyObservationsAdded() override { invocation_count++; }

  int invocation_count = 0;
};

// A mock ConsistentProtoStore. Its Read() and Write() methods increment
// counts of their invocations.
class MockConsistentProtoStore : public ::cobalt::util::ConsistentProtoStore {
 public:
  explicit MockConsistentProtoStore(std::string filename)
      : ::cobalt::util::ConsistentProtoStore(
            std::move(filename),
            std::make_unique<::cobalt::util::PosixFileSystem>()) {
    read_count_ = 0;
    write_count_ = 0;
  }

  ~MockConsistentProtoStore() override = default;

  // To set the stored proto in a test, use |set_stored_proto| instead of Write.
  ::cobalt::util::Status Write(
      const google::protobuf::MessageLite& /*proto*/) override {
    write_count_++;
    return ::cobalt::util::Status::OK;
  }

  ::cobalt::util::Status Read(google::protobuf::MessageLite* proto) override {
    if (stored_proto_) {
      proto->Clear();
      proto->CheckTypeAndMergeFrom(*stored_proto_);
    }
    read_count_++;
    return ::cobalt::util::Status::OK;
  }

  void ResetCounts() {
    read_count_ = 0;
    write_count_ = 0;
  }

  int read_count_;   // NOLINT
  int write_count_;  // NOLINT

  void set_stored_proto(std::unique_ptr<google::protobuf::MessageLite> proto) {
    stored_proto_ = std::move(proto);
  }

 private:
  std::unique_ptr<google::protobuf::MessageLite> stored_proto_;
};

// Creates and returns a ProjectContext from a serialized, base64-encoded Cobalt
// registry.
std::unique_ptr<ProjectContext> GetTestProject(
    const std::string& registry_base64);

// Returns the ReportAggregationKey associated to a report, given a
// ProjectContext containing the report and the report's MetricReportId.
ReportAggregationKey MakeAggregationKey(const ProjectContext& project_context,
                                        const MetricReportId& metric_report_id);

// Returns the AggregationConfig associated to a report, given a ProjectContext
// containing the report and the report's MetricReportId.
AggregationConfig MakeAggregationConfig(const ProjectContext& project_context,
                                        const MetricReportId& metric_report_id);

// Given an ExpectedAggregationParams struct populated with information about
// the locally aggregated reports in a config, return an
// ExpectedUniqueActivesObservations map initialized with that config's
// MetricReportIds and window sizes and with a specified day index, with all
// activity indicators set to false.
//
// The ExpectedUniqueActivesObservations map generated by
// MakeNullExpectedObservations represents the set of Observations that should
// be generated for |day_index| in the case where no activity has been logged
// for any report and where no backfill is needed.
ExpectedUniqueActivesObservations MakeNullExpectedUniqueActivesObservations(
    const ExpectedAggregationParams& expected_params, uint32_t day_index);

// Given an ExpectedAggregationParams struct |expected_params|, return an
// ExpectedReportParticipationObservations containing a pair
// (|metric_report_id|, |day_index|) for each MetricReportId |metric_report_id|
// in |expected_params|.
ExpectedReportParticipationObservations
MakeExpectedReportParticipationObservations(
    const ExpectedAggregationParams& expected_params, uint32_t day_index);

// Populates |observations| with the contents of a FakeObservationStore.
// |observations| should be a vector whose size is equal to the number
// of expected observations. Checks the the ObservationStore contains
// the expected number of Observations and that the report_ids of the
// Observations are equal to |expected_report_ids|. Returns true iff all checks
// pass.
bool FetchObservations(std::vector<Observation2>* observations,
                       const std::vector<uint32_t>& expected_report_ids,
                       FakeObservationStore* observation_store,
                       TestUpdateRecipient* update_recipient);

// Populates |observation| with the contents of a FakeObservationStore,
// which is expected to contain a single Observation with a report_id
// of |expected_report_id|. Returns true iff all checks pass.
bool FetchSingleObservation(Observation2* observation,
                            uint32_t expected_report_id,
                            FakeObservationStore* observation_store,
                            TestUpdateRecipient* update_recipient);

// Given an ExpectedAggregationParams containing information about the set of
// locally aggregated reports in a config, populates a vector |observations|
// with the contents of a FakeObservationStore and checks that the vector
// contains exactly the number of Observations that the EventAggregator should
// generate for a single day index, for each locally aggregated report in that
// config. Does not assume that the contents of the FakeObservationStore have a
// particular order. The size of |observations| is ignored, and can be 0.
bool FetchAggregatedObservations(
    std::vector<Observation2>* observations,
    const ExpectedAggregationParams& expected_params,
    FakeObservationStore* observation_store,
    TestUpdateRecipient* update_recipient);

// Checks that the contents of a FakeObservationStore is a sequence of
// IntegerEventObservations specified by the various parameters. Returns
// true if all checks pass.
bool CheckNumericEventObservations(
    const std::vector<uint32_t>& expected_report_ids,
    uint32_t expected_event_code, const std::string& expected_component_name,
    int64_t expected_int_value, FakeObservationStore* observation_store,
    TestUpdateRecipient* update_recipient);

// Checks that the Observations contained in a FakeObservationStore are exactly
// the UniqueActivesObservations that should be generated for a single day index
// given a representation of the expected activity indicators for that day, for
// each UniqueActives report, for each window size and event code, for a config
// whose locally aggregated reports are all of type UNIQUE_N_DAY_ACTIVES.
bool CheckUniqueActivesObservations(
    const ExpectedUniqueActivesObservations& expected_obs,
    FakeObservationStore* observation_store,
    TestUpdateRecipient* update_recipient);

// Checks that the Observations contained in a FakeObservationStore are exactly
// the PerDeviceNumericObservations and ReportParticipationObservations that
// should be generated for a single day index given a representation of the
// expected activity indicators for that day, for each PER_DEVICE_NUMERIC_STATS
// report, for each window size and event code, for a config whose locally
// aggregated reports are all of type PER_DEVICE_NUMERIC_STATS.
bool CheckPerDeviceNumericObservations(
    ExpectedPerDeviceNumericObservations expected_per_device_numeric_obs,
    ExpectedReportParticipationObservations expected_report_participation_obs,
    FakeObservationStore* observation_store,
    TestUpdateRecipient* update_recipient);

}  // namespace testing
}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_LOGGER_LOGGER_TEST_UTILS_H_

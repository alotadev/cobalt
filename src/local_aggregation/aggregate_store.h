// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LOCAL_AGGREGATION_AGGREGATE_STORE_H_
#define COBALT_SRC_LOCAL_AGGREGATION_AGGREGATE_STORE_H_

#include <condition_variable>
#include <memory>
#include <string>
#include <thread>

#include "src/lib/util/consistent_proto_store.h"
#include "src/lib/util/protected_fields.h"
#include "src/local_aggregation/local_aggregation.pb.h"
#include "src/logger/encoder.h"
#include "src/logger/observation_writer.h"
#include "src/logger/project_context.h"
#include "src/logger/status.h"

namespace cobalt {

// Forward declaration used for friend tests. This will be removed.
// TODO(ninai): remove this
namespace logger {

class TestEventAggregator;

}  // namespace logger

namespace local_aggregation {

const std::chrono::hours kOneDay(24);

// Maximum value of |backfill_days| allowed by the constructor.
constexpr size_t kMaxAllowedBackfillDays = 1000;
// All aggregation windows larger than this number of days are ignored.
constexpr uint32_t kMaxAllowedAggregationDays = 365;
// All hourly aggregation windows larger than this number of hours are ignored.
constexpr uint32_t kMaxAllowedAggregationHours = 23;

// The current version number of the LocalAggregateStore.
constexpr uint32_t kCurrentLocalAggregateStoreVersion = 1;
// The current version number of the AggregatedObservationHistoryStore.
constexpr uint32_t kCurrentObservationHistoryStoreVersion = 0;

// The AggregateStore manages an in-memory store of aggregated Event values, indexed by report,
// day index, and other dimensions specific to the report type (e.g. event code).
//
// When GenerateObservations() is called, this data is used to generate Observations representing
// aggregates of Event values over a day, week, month, etc.
//
// This class also exposes a GarbageCollect*() and Backup*() functionality which deletes
// unnecessary data and backs up the store respectively.
class AggregateStore {
 public:
  // Constructs an AggregateStore.
  //
  // An AggregateStore maintains daily aggregates of Events in a
  // LocalAggregateStore, uses an Encoder to generate Observations for rolling
  // windows ending on a specified day index, and writes the Observations to
  // an ObservationStore using an ObservationWriter.
  //
  // encoder: the singleton instance of an Encoder on the system.
  //
  // local_aggregate_proto_store: A ConsistentProtoStore to be used by the
  // AggregateStore to store snapshots of its in-memory store of event
  // aggregates.
  //
  // obs_history_proto_store: A ConsistentProtoStore to be used by the
  // AggregateStore to store snapshots of its in-memory history of generated
  // Observations.
  //
  // backfill_days: the number of past days for which the AggregateStore
  // generates and sends Observations, in addition to a requested day index.
  // See the comment above GenerateObservations for more detail. The constructor CHECK-fails if a
  // value larger than |kMaxAllowedBackfillDays| is passed.
  AggregateStore(const logger::Encoder* encoder,
                 const logger::ObservationWriter* observation_writer,
                 util::ConsistentProtoStore* local_aggregate_proto_store,
                 util::ConsistentProtoStore* obs_history_proto_store, size_t backfill_days = 0);

  // Given a ProjectContext, MetricDefinition, and ReportDefinition and a pointer
  // to the LocalAggregateStore, checks whether a key with the same customer,
  // project, metric, and report ID already exists in the LocalAggregateStore. If
  // not, creates and inserts a new key and value. Returns kInvalidArguments if
  // creation of the key or value fails, and kOK otherwise. The caller should hold
  // the mutex protecting the LocalAggregateStore.
  logger::Status MaybeInsertReportConfigLocked(const logger::ProjectContext& project_context,
                                               const MetricDefinition& metric,
                                               const ReportDefinition& report,
                                               LocalAggregateStore* store);

  // Writes a snapshot of the LocalAggregateStore to
  // |local_aggregate_proto_store_|.
  logger::Status BackUpLocalAggregateStore();

  // Writes a snapshot of |obs_history_|to |obs_history_proto_store_|.
  logger::Status BackUpObservationHistory();

  // Removes from the LocalAggregateStore all daily aggregates that are too
  // old to contribute to their parent report's largest rolling window on the
  // day which is |backfill_days| before |day_index_utc| (if the parent
  // MetricDefinitions' time zone policy is UTC) or which is |backfill_days|
  // before |day_index_local| (if the parent MetricDefinition's time zone policy
  // is LOCAL). If |day_index_local| is 0, then we set |day_index_local| =
  // |day_index_utc|.
  //
  // If the time zone policy of a report's parent metric is UTC (resp., LOCAL)
  // and if day_index is the largest value of the |day_index_utc| (resp.,
  // |day_index_local|) argument with which GarbageCollect() has been called,
  // then the LocalAggregateStore contains the data needed to generate
  // Observations for that report for day index (day_index + k) for any k >= 0.
  logger::Status GarbageCollect(uint32_t day_index_utc, uint32_t day_index_local = 0u);

  // Generates one or more Observations for all of the registered locally
  // aggregated reports known to this AggregateStore, for rolling windows
  // ending on specified day indices.
  //
  // Each MetricDefinition specifies a time zone policy, which determines the
  // day index for which an Event associated with that MetricDefinition is
  // logged. For all MetricDefinitions whose Events are logged with respect to
  // UTC, this method generates Observations for rolling windows ending on
  // |final_day_index_utc|. For all MetricDefinitions whose Events are logged
  // with respect to local time, this method generates Observations for rolling
  // windows ending on |final_day_index_local|. If |final_day_index_local| is
  // 0, then we set |final_day_index_local| = |final_day_index_utc|.
  //
  // The generated Observations are written to the |observation_writer| passed
  // to the constructor.
  //
  // This class maintains a history of generated Observations and this method
  // additionally performs backfill: Observations are also generated for
  // rolling windows ending on any day in the interval [final_day_index -
  // backfill_days, final_day_index] (where final_day_index is either
  // final_day_index_utc or final_day_index_local, depending on the time zone
  // policy of the associated MetricDefinition), if this history indicates they
  // were not previously generated. Does not generate duplicate Observations
  // when called multiple times with the same day index.
  //
  // Observations are not generated for aggregation windows larger than
  // |kMaxAllowedAggregationDays|. Hourly windows are not yet supported.
  logger::Status GenerateObservations(uint32_t final_day_index_utc,
                                      uint32_t final_day_index_local = 0u);

 private:
  friend class EventAggregator;  // used for transition during redesign.
  friend class AggregateStoreTest;
  friend class EventAggregatorTest;
  friend class EventAggregatorManagerTest;
  friend class logger::TestEventAggregator;

  // Make a LocalAggregateStore which is empty except that its version number is set to |version|.
  LocalAggregateStore MakeNewLocalAggregateStore(
      uint32_t version = kCurrentLocalAggregateStoreVersion);

  // Make an AggregatedObservationHistoryStore which is empty except that its version number is set
  // to |version|.
  AggregatedObservationHistoryStore MakeNewObservationHistoryStore(
      uint32_t version = kCurrentObservationHistoryStoreVersion);

  // The LocalAggregateStore or AggregatedObservationHistoryStore may need to be changed in ways
  // which are structurally but not semantically backwards-compatible. In other words, the meaning
  // to the AggregateStore of a field in the LocalAggregateStore might change. An example is that
  // we might deprecate one field while introducing a new one.
  //
  // The MaybeUpgrade*Store methods allow the AggregateStore to update the contents of its stored
  // protos from previously meaningful values to currently meaningful values. (For example, a
  // possible implementation would move the contents of a deprecated field to the replacement
  // field.)
  //
  // These methods are called by the AggregateStore constructor immediately after reading in stored
  // protos from disk in order to ensure that proto contents have the expected semantics.
  //
  // The method first checks the version number of the store. If the version number is equal to
  // |kCurrentLocalAggregateStoreVersion| or |kCurrentObservationHistoryStoreVersion|
  // (respectively), returns an OK status. Otherwise, if it is possible to upgrade the store to the
  // current version, does so and returns an OK status. If not, logs an error and returns
  // kInvalidArguments. If a non-OK status is returned, the caller should discard the contents of
  // |store| and replace it with an empty store at the current version. The MakeNew*Store() methods
  // may be used to create the new store.
  logger::Status MaybeUpgradeLocalAggregateStore(LocalAggregateStore* store);
  logger::Status MaybeUpgradeObservationHistoryStore(AggregatedObservationHistoryStore* store);

  // Returns the most recent day index for which an Observation was generated
  // for a given UNIQUE_N_DAY_ACTIVES report, event code, and day-based aggregation window,
  // according to |obs_history_|. Returns 0 if no Observation has been generated
  // for the given arguments.
  uint32_t UniqueActivesLastGeneratedDayIndex(const std::string& report_key, uint32_t event_code,
                                              uint32_t aggregation_days) const;

  // Returns the most recent day index for which an Observation was generated for a given
  // PER_DEVICE_NUMERIC_STATS or PER_DEVICE_HISTOGRAM report, component, event code, and day-based
  // aggregation window, according to |obs_history_|. Returns 0 if no Observation has been generated
  // for the given arguments.
  uint32_t PerDeviceNumericLastGeneratedDayIndex(const std::string& report_key,
                                                 const std::string& component, uint32_t event_code,
                                                 uint32_t aggregation_days) const;

  // Returns the most recent day index for which a
  // ReportParticipationObservation was generated for a given report, according
  // to |obs_history_|. Returns 0 if no Observation has been generated for the
  // given arguments.
  uint32_t ReportParticipationLastGeneratedDayIndex(const std::string& report_key) const;

  // For a fixed report of type UNIQUE_N_DAY_ACTIVES, generates an Observation
  // for each event code of the parent metric, for each day-based aggregation window of the
  // report ending on |final_day_index|, unless an Observation with those parameters was generated
  // in the past. Also generates Observations for days in the backfill period if needed. Writes the
  // Observations to an ObservationStore via the ObservationWriter that was passed to the
  // constructor.
  //
  // Observations are not generated for aggregation windows larger than
  // |kMaxAllowedAggregationDays|. Hourly windows are not yet supported.
  logger::Status GenerateUniqueActivesObservations(logger::MetricRef metric_ref,
                                                   const std::string& report_key,
                                                   const ReportAggregates& report_aggregates,
                                                   uint32_t num_event_codes,
                                                   uint32_t final_day_index);

  // Helper method called by GenerateUniqueActivesObservations() to generate
  // and write a single Observation.
  logger::Status GenerateSingleUniqueActivesObservation(logger::MetricRef metric_ref,
                                                        const ReportDefinition* report,
                                                        uint32_t obs_day_index, uint32_t event_code,
                                                        const OnDeviceAggregationWindow& window,
                                                        bool was_active) const;

  // For a fixed report of type PER_DEVICE_NUMERIC_STATS or PER_DEVICE_HISTOGRAM, generates a
  // PerDeviceNumericObservation and PerDeviceHistogramObservation respectively for each
  // tuple (component, event code, aggregation_window) for which a numeric event was logged for that
  // event code and component during the window of that size ending on |final_day_index|, unless an
  // Observation with those parameters has been generated in the past. The value of the Observation
  // is the sum, max, or min (depending on the aggregation_type field of the report definition) of
  // all numeric events logged for that report during the window. Also generates observations for
  // days in the backfill period if needed.
  //
  // In addition to PerDeviceNumericObservations or PerDeviceHistogramObservation , generates
  // a ReportParticipationObservation for |final_day_index| and any needed days in the backfill
  // period. These ReportParticipationObservations are used by the report generators to infer the
  // fleet-wide number of devices for which the sum of numeric events associated to each tuple
  // (component, event code, window size) was zero.
  //
  // Observations are not generated for aggregation windows larger than
  // |kMaxAllowedAggregationWindowSize|.
  logger::Status GenerateObsFromNumericAggregates(logger::MetricRef metric_ref,
                                                  const std::string& report_key,
                                                  const ReportAggregates& report_aggregates,
                                                  uint32_t final_day_index);

  // Helper method called by GenerateObsFromNumericAggregates() to generate and write a single
  // Observation with value |value|. The method will produce a PerDeviceNumericObservation or
  // PerDeviceHistogramObservation  depending on whether the report type is
  // PER_DEVICE_NUMERIC_STATS or PER_DEVICE_HISTOGRAM respectively.
  logger::Status GenerateSinglePerDeviceNumericObservation(
      logger::MetricRef metric_ref, const ReportDefinition* report, uint32_t obs_day_index,
      const std::string& component, uint32_t event_code, const OnDeviceAggregationWindow& window,
      int64_t value) const;

  // Helper method called by GenerateObsFromNumericAggregates() to generate and write a single
  // Observation with value |value|.
  logger::Status GenerateSinglePerDeviceHistogramObservation(
      logger::MetricRef metric_ref, const ReportDefinition* report, uint32_t obs_day_index,
      const std::string& component, uint32_t event_code, const OnDeviceAggregationWindow& window,
      int64_t value) const;

  // Helper method called by GenerateObsFromNumericAggregates() to generate and write a single
  // ReportParticipationObservation.
  logger::Status GenerateSingleReportParticipationObservation(logger::MetricRef metric_ref,
                                                              const ReportDefinition* report,
                                                              uint32_t obs_day_index) const;

  LocalAggregateStore CopyLocalAggregateStore() {
    auto local_aggregate_store = protected_aggregate_store_.lock()->local_aggregate_store;
    return local_aggregate_store;
  }

  struct AggregateStoreFields {
    LocalAggregateStore local_aggregate_store;
  };

  const logger::Encoder* encoder_;
  const logger::ObservationWriter* observation_writer_;
  util::ConsistentProtoStore* local_aggregate_proto_store_;
  util::ConsistentProtoStore* obs_history_proto_store_;
  util::ProtectedFields<AggregateStoreFields> protected_aggregate_store_;
  // Not protected by a mutex. Should only be accessed by the Event Aggregator's |worker_thread_|.
  AggregatedObservationHistoryStore obs_history_;
  size_t backfill_days_ = 0;
};

}  // namespace local_aggregation
}  // namespace cobalt

#endif  // COBALT_SRC_LOCAL_AGGREGATION_AGGREGATE_STORE_H_

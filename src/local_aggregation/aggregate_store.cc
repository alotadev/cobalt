// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/local_aggregation/aggregate_store.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/algorithms/rappor/rappor_config_helper.h"
#include "src/lib/util/datetime_util.h"
#include "src/lib/util/proto_util.h"
#include "src/lib/util/status.h"
#include "src/local_aggregation/aggregation_utils.h"
#include "src/registry/packed_event_codes.h"

namespace cobalt::local_aggregation {

using google::protobuf::RepeatedField;
using logger::Encoder;
using logger::kInvalidArguments;
using logger::kOK;
using logger::kOther;
using logger::MetricRef;
using logger::ObservationWriter;
using logger::ProjectContext;
using logger::Status;
using rappor::RapporConfigHelper;
using util::ConsistentProtoStore;
using util::SerializeToBase64;
using util::StatusCode;

namespace {

////// General helper functions.

// Populates a ReportAggregationKey proto message and then populates a string
// with the base64 encoding of the serialized proto.
bool PopulateReportKey(uint32_t customer_id, uint32_t project_id, uint32_t metric_id,
                       uint32_t report_id, std::string* key) {
  ReportAggregationKey key_data;
  key_data.set_customer_id(customer_id);
  key_data.set_project_id(project_id);
  key_data.set_metric_id(metric_id);
  key_data.set_report_id(report_id);
  return SerializeToBase64(key_data, key);
}

////// Helper functions used by the constructor and UpdateAggregationConfigs().

// Gets and validates the window sizes and/or aggregation windows from a ReportDefinition, converts
// window sizes to daily aggregation windows, sorts the aggregation windows in increasing order, and
// adds them to an AggregationConfig.
//
// TODO(pesk): Stop looking at the window_size field of |report| once all reports have been updated
// to have OnDeviceAggregationWindows only.
bool GetSortedAggregationWindowsFromReport(const ReportDefinition& report,
                                           AggregationConfig* aggregation_config) {
  if (report.window_size_size() == 0 && report.aggregation_window_size() == 0) {
    LOG(ERROR) << "Report must have at least one window size or aggregation window.";
    return false;
  }
  std::vector<uint32_t> aggregation_days;
  std::vector<uint32_t> aggregation_hours;
  for (const uint32_t window_size : report.window_size()) {
    if (window_size == 0 || window_size > kMaxAllowedAggregationDays) {
      LOG(ERROR) << "Window size must be positive and cannot exceed " << kMaxAllowedAggregationDays;
      return false;
    }
    aggregation_days.push_back(window_size);
  }
  for (const auto& window : report.aggregation_window()) {
    switch (window.units_case()) {
      case OnDeviceAggregationWindow::kDays: {
        uint32_t num_days = window.days();
        if (num_days == 0 || num_days > kMaxAllowedAggregationDays) {
          LOG(ERROR) << "Daily windows must contain at least 1 and no more than "
                     << kMaxAllowedAggregationDays << " days";
          return false;
        }
        aggregation_days.push_back(num_days);
        break;
      }
      case OnDeviceAggregationWindow::kHours: {
        uint32_t num_hours = window.hours();
        if (num_hours == 0 || num_hours > kMaxAllowedAggregationHours) {
          LOG(ERROR) << "Hourly windows must contain at least 1 and no more than "
                     << kMaxAllowedAggregationHours << " hours";
          return false;
        }
        aggregation_hours.push_back(num_hours);
        break;
      }
      default:
        LOG(ERROR) << "Invalid OnDeviceAggregationWindow type " << window.units_case();
    }
  }
  std::sort(aggregation_hours.begin(), aggregation_hours.end());
  std::sort(aggregation_days.begin(), aggregation_days.end());
  for (auto num_hours : aggregation_hours) {
    *aggregation_config->add_aggregation_window() = MakeHourWindow(num_hours);
  }
  for (auto num_days : aggregation_days) {
    *aggregation_config->add_aggregation_window() = MakeDayWindow(num_days);
  }
  return true;
}

// Creates an AggregationConfig from a ProjectContext, MetricDefinition, and
// ReportDefinition and populates the aggregation_config field of a specified
// ReportAggregates. Also sets the type of the ReportAggregates based on the
// ReportDefinition's type.
//
// Accepts ReportDefinitions with either at least one WindowSize, or at least one
// OnDeviceAggregationWindow with units in days.
bool PopulateReportAggregates(const ProjectContext& project_context, const MetricDefinition& metric,
                              const ReportDefinition& report, ReportAggregates* report_aggregates) {
  if (report.window_size_size() == 0 && report.aggregation_window_size() == 0) {
  }
  AggregationConfig* aggregation_config = report_aggregates->mutable_aggregation_config();
  *aggregation_config->mutable_project() = project_context.project();
  *aggregation_config->mutable_metric() = *project_context.GetMetric(metric.id());
  *aggregation_config->mutable_report() = report;
  if (!GetSortedAggregationWindowsFromReport(report, aggregation_config)) {
    return false;
  }
  switch (report.report_type()) {
    case ReportDefinition::UNIQUE_N_DAY_ACTIVES: {
      report_aggregates->set_allocated_unique_actives_aggregates(new UniqueActivesReportAggregates);
      return true;
    }
    case ReportDefinition::PER_DEVICE_NUMERIC_STATS:
    case ReportDefinition::PER_DEVICE_HISTOGRAM: {
      report_aggregates->set_allocated_numeric_aggregates(new PerDeviceNumericAggregates);
      return true;
    }
    default:
      return false;
  }
}

// Move all items from the |window_size| field to the |aggregation_window| field
// of each AggregationConfig, preserving the order of the items. The |aggregation_window| field
// should be empty if the |window_size| field is nonempty. If for some reason this is not true, log
// an error and discard the contents of |aggregation_window| and replace them with the migrated
// |window_size| values.
void ConvertWindowSizesToAggregationDays(LocalAggregateStore* store) {
  for (auto [key, aggregates] : store->by_report_key()) {
    auto config = (*store->mutable_by_report_key())[key].mutable_aggregation_config();
    if (config->window_size_size() > 0 && config->aggregation_window_size() > 0) {
      LOG(ERROR) << "Config has both a window_size and an aggregation_window; discarding all "
                    "aggregation_windows";
      config->clear_aggregation_window();
    }
    for (auto window_size : config->window_size()) {
      *config->add_aggregation_window() = MakeDayWindow(window_size);
    }
    config->clear_window_size();
  }
}

// Upgrades the LocalAggregateStore from version 0 to |kCurrentLocalAggregateStoreVersion|.
Status UpgradeLocalAggregateStoreFromVersion0(LocalAggregateStore* store) {
  ConvertWindowSizesToAggregationDays(store);
  store->set_version(kCurrentLocalAggregateStoreVersion);
  return kOK;
}

}  // namespace

AggregateStore::AggregateStore(const Encoder* encoder, const ObservationWriter* observation_writer,
                               ConsistentProtoStore* local_aggregate_proto_store,
                               ConsistentProtoStore* obs_history_proto_store,
                               const size_t backfill_days)
    : encoder_(encoder),
      observation_writer_(observation_writer),
      local_aggregate_proto_store_(local_aggregate_proto_store),
      obs_history_proto_store_(obs_history_proto_store) {
  CHECK_LE(backfill_days, kMaxAllowedBackfillDays)
      << "backfill_days must be less than or equal to " << kMaxAllowedBackfillDays;
  backfill_days_ = backfill_days;
  auto locked_store = protected_aggregate_store_.lock();
  locked_store->empty_local_aggregate_store = MakeNewLocalAggregateStore();
  auto restore_aggregates_status =
      local_aggregate_proto_store_->Read(&(locked_store->local_aggregate_store));
  switch (restore_aggregates_status.error_code()) {
    case StatusCode::OK: {
      VLOG(4) << "Read LocalAggregateStore from disk.";
      break;
    }
    case StatusCode::NOT_FOUND: {
      VLOG(4) << "No file found for local_aggregate_proto_store. Proceeding "
                 "with empty LocalAggregateStore. File will be created on "
                 "first snapshot of the LocalAggregateStore.";
      locked_store->local_aggregate_store = MakeNewLocalAggregateStore();
      break;
    }
    default: {
      LOG(ERROR) << "Read to local_aggregate_proto_store failed with status code: "
                 << restore_aggregates_status.error_code()
                 << "\nError message: " << restore_aggregates_status.error_message()
                 << "\nError details: " << restore_aggregates_status.error_details()
                 << "\nProceeding with empty LocalAggregateStore.";
      locked_store->local_aggregate_store = MakeNewLocalAggregateStore();
    }
  }
  if (auto status = MaybeUpgradeLocalAggregateStore(&(locked_store->local_aggregate_store));
      status != kOK) {
    LOG(ERROR) << "Failed to upgrade LocalAggregateStore to current version with status " << status
               << ".\nProceeding with empty "
                  "LocalAggregateStore.";
    locked_store->local_aggregate_store = MakeNewLocalAggregateStore();
  }

  auto locked_obs_history = protected_obs_history_.lock();
  auto restore_history_status = obs_history_proto_store_->Read(&locked_obs_history->obs_history);
  switch (restore_history_status.error_code()) {
    case StatusCode::OK: {
      VLOG(4) << "Read AggregatedObservationHistoryStore from disk.";
      break;
    }
    case StatusCode::NOT_FOUND: {
      VLOG(4) << "No file found for obs_history_proto_store. Proceeding "
                 "with empty AggregatedObservationHistoryStore. File will be "
                 "created on first snapshot of the AggregatedObservationHistoryStore.";
      break;
    }
    default: {
      LOG(ERROR) << "Read to obs_history_proto_store failed with status code: "
                 << restore_history_status.error_code()
                 << "\nError message: " << restore_history_status.error_message()
                 << "\nError details: " << restore_history_status.error_details()
                 << "\nProceeding with empty AggregatedObservationHistoryStore.";
      locked_obs_history->obs_history = MakeNewObservationHistoryStore();
    }
  }
  if (auto status = MaybeUpgradeObservationHistoryStore(&locked_obs_history->obs_history);
      status != kOK) {
    LOG(ERROR)
        << "Failed to upgrade AggregatedObservationHistoryStore to current version with status "
        << status << ".\nProceeding with empty AggregatedObservationHistoryStore.";
    locked_obs_history->obs_history = MakeNewObservationHistoryStore();
  }
}

Status AggregateStore::MaybeInsertReportConfig(const ProjectContext& project_context,
                                               const MetricDefinition& metric,
                                               const ReportDefinition& report) {
  auto locked = protected_aggregate_store_.lock();
  std::string key;
  if (!PopulateReportKey(project_context.project().customer_id(),
                         project_context.project().project_id(), metric.id(), report.id(), &key)) {
    return kInvalidArguments;
  }
  ReportAggregates report_aggregates;
  if (locked->local_aggregate_store.by_report_key().count(key) == 0) {
    if (!PopulateReportAggregates(project_context, metric, report, &report_aggregates)) {
      return kInvalidArguments;
    }
    (*locked->local_aggregate_store.mutable_by_report_key())[key] = report_aggregates;
  }

  // Make sure that the 'empty' store has the key as well.
  ReportAggregates empty_report_aggregates;
  if (locked->empty_local_aggregate_store.by_report_key().count(key) == 0) {
    if (!PopulateReportAggregates(project_context, metric, report, &empty_report_aggregates)) {
      return kInvalidArguments;
    }
    (*locked->empty_local_aggregate_store.mutable_by_report_key())[key] = empty_report_aggregates;
  }
  return kOK;
}

Status AggregateStore::SetActive(uint32_t customer_id, uint32_t project_id, uint32_t metric_id,
                                 uint32_t report_id, uint64_t event_code, uint32_t day_index) {
  if (is_disabled_) {
    return kOK;
  }
  std::string key;
  if (!PopulateReportKey(customer_id, project_id, metric_id, report_id, &key)) {
    return kInvalidArguments;
  }

  auto locked = protected_aggregate_store_.lock();
  auto aggregates = locked->local_aggregate_store.mutable_by_report_key()->find(key);
  if (aggregates == locked->local_aggregate_store.mutable_by_report_key()->end()) {
    LOG(ERROR) << "The Local Aggregate Store received an unexpected key.";
    return kInvalidArguments;
  }
  if (!aggregates->second.has_unique_actives_aggregates()) {
    LOG(ERROR) << "The local aggregates for this report key are not of type "
                  "UniqueActivesReportAggregates.";
    return kInvalidArguments;
  }
  (*(*aggregates->second.mutable_unique_actives_aggregates()->mutable_by_event_code())[event_code]
        .mutable_by_day_index())[day_index]
      .mutable_activity_daily_aggregate()
      ->set_activity_indicator(true);
  return kOK;
}

Status AggregateStore::UpdateNumericAggregate(uint32_t customer_id, uint32_t project_id,
                                              uint32_t metric_id, uint32_t report_id,
                                              const std::string& component, uint64_t event_code,
                                              uint32_t day_index, int64_t value) {
  if (is_disabled_) {
    return kOK;
  }
  std::string report_key;
  if (!PopulateReportKey(customer_id, project_id, metric_id, report_id, &report_key)) {
    return kInvalidArguments;
  }

  auto locked = protected_aggregate_store_.lock();
  auto aggregates = locked->local_aggregate_store.mutable_by_report_key()->find(report_key);
  if (aggregates == locked->local_aggregate_store.mutable_by_report_key()->end()) {
    LOG(ERROR) << "The Local Aggregate Store received an unexpected key.";
    return kInvalidArguments;
  }
  if (!aggregates->second.has_numeric_aggregates()) {
    LOG(ERROR) << "The local aggregates for this report key are not of a "
                  "compatible type.";
    return kInvalidArguments;
  }

  auto aggregates_by_day =
      (*(*aggregates->second.mutable_numeric_aggregates()->mutable_by_component())[component]
            .mutable_by_event_code())[event_code]
          .mutable_by_day_index();
  bool has_stored_aggregate = ((*aggregates_by_day).find(day_index) != aggregates_by_day->end());
  auto day_aggregate = (*aggregates_by_day)[day_index].mutable_numeric_daily_aggregate();

  auto [status, updated_value] = GetUpdatedAggregate(
      aggregates->second.aggregation_config().report().aggregation_type(),
      has_stored_aggregate ? std::optional<int64_t>{day_aggregate->value()} : std::nullopt, value);
  if (status == kOK) {
    day_aggregate->set_value(updated_value);
    return kOK;
  }

  return status;
}

RepeatedField<uint32_t> UnpackEventCodesProto(uint64_t packed_event_codes) {
  RepeatedField<uint32_t> fields;
  for (auto code : config::UnpackEventCodes(packed_event_codes)) {
    *fields.Add() = code;
  }
  return fields;
}

Status AggregateStore::BackUpLocalAggregateStore() {
  // Lock, copy the LocalAggregateStore, and release the lock. Write the copy
  // to |local_aggregate_proto_store_|.
  auto local_aggregate_store = CopyLocalAggregateStore();
  auto status = local_aggregate_proto_store_->Write(local_aggregate_store);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to back up the LocalAggregateStore with error code: "
               << status.error_code() << "\nError message: " << status.error_message()
               << "\nError details: " << status.error_details();
    return kOther;
  }
  return kOK;
}

Status AggregateStore::BackUpObservationHistory() {
  auto obs_history = protected_obs_history_.lock()->obs_history;
  auto status = obs_history_proto_store_->Write(obs_history);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to back up the AggregatedObservationHistoryStore. "
                  "::cobalt::util::Status error code: "
               << status.error_code() << "\nError message: " << status.error_message()
               << "\nError details: " << status.error_details();
    return kOther;
  }
  return kOK;
}

////////////////////// GarbageCollect and helper functions //////////////////

namespace {

void GarbageCollectUniqueActivesReportAggregates(uint32_t day_index, uint32_t max_aggregation_days,
                                                 uint32_t backfill_days,
                                                 UniqueActivesReportAggregates* report_aggregates) {
  auto map_by_event_code = report_aggregates->mutable_by_event_code();
  for (auto event_code = map_by_event_code->begin(); event_code != map_by_event_code->end();) {
    auto map_by_day = event_code->second.mutable_by_day_index();
    for (auto day = map_by_day->begin(); day != map_by_day->end();) {
      if (day->first <= day_index - backfill_days - max_aggregation_days) {
        day = map_by_day->erase(day);
      } else {
        ++day;
      }
    }
    if (map_by_day->empty()) {
      event_code = map_by_event_code->erase(event_code);
    } else {
      ++event_code;
    }
  }
}

void GarbageCollectNumericReportAggregates(uint32_t day_index, uint32_t max_aggregation_days,
                                           uint32_t backfill_days,
                                           PerDeviceNumericAggregates* report_aggregates) {
  auto map_by_component = report_aggregates->mutable_by_component();
  for (auto component = map_by_component->begin(); component != map_by_component->end();) {
    auto map_by_event_code = component->second.mutable_by_event_code();
    for (auto event_code = map_by_event_code->begin(); event_code != map_by_event_code->end();) {
      auto map_by_day = event_code->second.mutable_by_day_index();
      for (auto day = map_by_day->begin(); day != map_by_day->end();) {
        if (day->first <= day_index - backfill_days - max_aggregation_days) {
          day = map_by_day->erase(day);
        } else {
          ++day;
        }
      }
      if (map_by_day->empty()) {
        event_code = map_by_event_code->erase(event_code);
      } else {
        ++event_code;
      }
    }
    if (map_by_event_code->empty()) {
      component = map_by_component->erase(component);
    } else {
      ++component;
    }
  }
}

}  // namespace

Status AggregateStore::GarbageCollect(uint32_t day_index_utc, uint32_t day_index_local) {
  if (day_index_local == 0u) {
    day_index_local = day_index_utc;
  }
  CHECK_LT(day_index_utc, UINT32_MAX);
  CHECK_LT(day_index_local, UINT32_MAX);
  CHECK_GE(day_index_utc, kMaxAllowedAggregationDays + backfill_days_);
  CHECK_GE(day_index_local, kMaxAllowedAggregationDays + backfill_days_);

  auto locked = protected_aggregate_store_.lock();
  for (const auto& [report_key, aggregates] : locked->local_aggregate_store.by_report_key()) {
    uint32_t day_index;
    const auto& config = aggregates.aggregation_config();
    switch (config.metric().time_zone_policy()) {
      case MetricDefinition::UTC: {
        day_index = day_index_utc;
        break;
      }
      case MetricDefinition::LOCAL: {
        day_index = day_index_local;
        break;
      }
      default:
        LOG_FIRST_N(ERROR, 10) << "The TimeZonePolicy of this MetricDefinition is invalid.";
        continue;
    }
    if (aggregates.aggregation_config().aggregation_window_size() == 0) {
      LOG_FIRST_N(ERROR, 10) << "This ReportDefinition does not have an aggregation window.";
      continue;
    }
    // PopulateReportAggregates ensured that aggregation_window has at least one element, that all
    // aggregation windows are <= kMaxAllowedAggregationDays, and that config.aggregation_window()
    // is sorted in increasing order.
    uint32_t max_aggregation_days = 1u;
    const OnDeviceAggregationWindow& largest_window =
        config.aggregation_window(config.aggregation_window_size() - 1);
    if (largest_window.units_case() == OnDeviceAggregationWindow::kDays) {
      max_aggregation_days = largest_window.days();
    }
    if (max_aggregation_days == 0u || max_aggregation_days > day_index) {
      LOG_FIRST_N(ERROR, 10) << "The maximum number of aggregation days " << max_aggregation_days
                             << " of this ReportDefinition is out of range.";
      continue;
    }
    // For each ReportAggregates, descend to and iterate over the sub-map of
    // local aggregates keyed by day index. Keep buckets with day indices
    // greater than |day_index| - |backfill_days_| - |max_aggregation_days|, and
    // remove all buckets with smaller day indices.
    switch (aggregates.type_case()) {
      case ReportAggregates::kUniqueActivesAggregates: {
        GarbageCollectUniqueActivesReportAggregates(
            day_index, max_aggregation_days, backfill_days_,
            locked->local_aggregate_store.mutable_by_report_key()
                ->at(report_key)
                .mutable_unique_actives_aggregates());
        break;
      }
      case ReportAggregates::kNumericAggregates: {
        GarbageCollectNumericReportAggregates(day_index, max_aggregation_days, backfill_days_,
                                              locked->local_aggregate_store.mutable_by_report_key()
                                                  ->at(report_key)
                                                  .mutable_numeric_aggregates());
        break;
      }
      default:
        continue;
    }
  }
  return kOK;
}

Status AggregateStore::GenerateObservations(uint32_t final_day_index_utc,
                                            uint32_t final_day_index_local) {
  if (final_day_index_local == 0u) {
    final_day_index_local = final_day_index_utc;
  }
  CHECK_LT(final_day_index_utc, UINT32_MAX);
  CHECK_LT(final_day_index_local, UINT32_MAX);
  CHECK_GE(final_day_index_utc, kMaxAllowedAggregationDays + backfill_days_);
  CHECK_GE(final_day_index_local, kMaxAllowedAggregationDays + backfill_days_);

  // Lock, copy the LocalAggregateStore, and release the lock. Use the copy to
  // generate observations.
  auto local_aggregate_store = CopyLocalAggregateStore();
  for (const auto& [report_key, aggregates] : local_aggregate_store.by_report_key()) {
    const auto& config = aggregates.aggregation_config();

    const auto& metric = config.metric();
    auto metric_ref = MetricRef(&config.project(), &metric);
    uint32_t final_day_index;
    switch (metric.time_zone_policy()) {
      case MetricDefinition::UTC: {
        final_day_index = final_day_index_utc;
        break;
      }
      case MetricDefinition::LOCAL: {
        final_day_index = final_day_index_local;
        break;
      }
      default:
        LOG_FIRST_N(ERROR, 10) << "The TimeZonePolicy of this MetricDefinition is invalid.";
        continue;
    }

    const auto& report = config.report();
    // PopulateReportAggregates ensured that aggregation_window has at least one element, that all
    // aggregation windows are <= kMaxAllowedAggregationDays, and that config.aggregation_window()
    // is sorted in increasing order.
    if (config.aggregation_window_size() == 0u) {
      LOG_FIRST_N(ERROR, 10) << "No aggregation_window found for this report.";
      continue;
    }
    uint32_t max_aggregation_days = 1u;
    const OnDeviceAggregationWindow& largest_window =
        config.aggregation_window(config.aggregation_window_size() - 1);
    if (largest_window.units_case() == OnDeviceAggregationWindow::kDays) {
      max_aggregation_days = largest_window.days();
    }
    if (max_aggregation_days == 0u || max_aggregation_days > final_day_index) {
      LOG_FIRST_N(ERROR, 10) << "The maximum number of aggregation days " << max_aggregation_days
                             << " of this ReportDefinition is out of range.";
      continue;
    }
    switch (metric.metric_type()) {
      case MetricDefinition::EVENT_OCCURRED: {
        auto num_event_codes = RapporConfigHelper::BasicRapporNumCategories(metric);

        switch (report.report_type()) {
          case ReportDefinition::UNIQUE_N_DAY_ACTIVES: {
            auto status = GenerateUniqueActivesObservations(metric_ref, report_key, aggregates,
                                                            num_event_codes, final_day_index);
            if (status != kOK) {
              return status;
            }
            break;
          }
          default:
            continue;
        }
        break;
      }
      case MetricDefinition::EVENT_COUNT:
      case MetricDefinition::ELAPSED_TIME:
      case MetricDefinition::FRAME_RATE:
      case MetricDefinition::MEMORY_USAGE: {
        switch (report.report_type()) {
          case ReportDefinition::PER_DEVICE_NUMERIC_STATS:
          case ReportDefinition::PER_DEVICE_HISTOGRAM: {
            auto status = GenerateObsFromNumericAggregates(metric_ref, report_key, aggregates,
                                                           final_day_index);
            if (status != kOK) {
              return status;
            }
            break;
          }
          default:
            continue;
        }
        break;
      }
      default:
        continue;
    }
  }
  return kOK;
}

////////// GenerateUniqueActivesObservations and helper methods ////////////////

namespace {

// Given the set of daily aggregates for a fixed event code, and the size and
// end date of an aggregation window, returns the first day index within that
// window on which the event code occurred. Returns 0 if the event code did
// not occur within the window.
uint32_t FirstActiveDayIndexInWindow(const DailyAggregates& daily_aggregates,
                                     uint32_t obs_day_index, uint32_t aggregation_days) {
  for (uint32_t day_index = obs_day_index - aggregation_days + 1; day_index <= obs_day_index;
       day_index++) {
    auto day_aggregate = daily_aggregates.by_day_index().find(day_index);
    if (day_aggregate != daily_aggregates.by_day_index().end() &&
        day_aggregate->second.activity_daily_aggregate().activity_indicator()) {
      return day_index;
    }
  }
  return 0u;
}

// Given the day index of an event occurrence and the size and end date
// of an aggregation window, returns true if the occurrence falls within
// the window and false if not.
bool IsActivityInWindow(uint32_t active_day_index, uint32_t obs_day_index,
                        uint32_t aggregation_days) {
  return (active_day_index <= obs_day_index && active_day_index > obs_day_index - aggregation_days);
}

}  // namespace

uint32_t AggregateStore::GetUniqueActivesLastGeneratedDayIndex(const std::string& report_key,
                                                               uint32_t event_code,
                                                               uint32_t aggregation_days) const {
  auto obs_history = protected_obs_history_.const_lock()->obs_history;
  auto report_history = obs_history.by_report_key().find(report_key);
  if (report_history == obs_history.by_report_key().end()) {
    return 0u;
  }
  auto event_code_history =
      report_history->second.unique_actives_history().by_event_code().find(event_code);
  if (event_code_history == report_history->second.unique_actives_history().by_event_code().end()) {
    return 0u;
  }
  auto window_history = event_code_history->second.by_window_size().find(aggregation_days);
  if (window_history == event_code_history->second.by_window_size().end()) {
    return 0u;
  }
  return window_history->second;
}

void AggregateStore::SetUniqueActivesLastGeneratedDayIndex(const std::string& report_key,
                                                           uint32_t event_code,
                                                           uint32_t aggregation_days,
                                                           uint32_t value) {
  auto locked = protected_obs_history_.lock();
  (*(*(*locked->obs_history.mutable_by_report_key())[report_key]
          .mutable_unique_actives_history()
          ->mutable_by_event_code())[event_code]
        .mutable_by_window_size())[aggregation_days] = value;
}

Status AggregateStore::GenerateSingleUniqueActivesObservation(
    const MetricRef metric_ref, const ReportDefinition* report, uint32_t obs_day_index,
    uint32_t event_code, const OnDeviceAggregationWindow& window, bool was_active) const {
  auto encoder_result = encoder_->EncodeUniqueActivesObservation(metric_ref, report, obs_day_index,
                                                                 event_code, was_active, window);
  if (encoder_result.status != kOK) {
    return encoder_result.status;
  }
  if (encoder_result.observation == nullptr || encoder_result.metadata == nullptr) {
    LOG(ERROR) << "Failed to encode UniqueActivesObservation";
    return kOther;
  }

  auto writer_status = observation_writer_->WriteObservation(*encoder_result.observation,
                                                             std::move(encoder_result.metadata));
  if (writer_status != kOK) {
    return writer_status;
  }
  return kOK;
}

Status AggregateStore::GenerateUniqueActivesObservations(const MetricRef metric_ref,
                                                         const std::string& report_key,
                                                         const ReportAggregates& report_aggregates,
                                                         uint32_t num_event_codes,
                                                         uint32_t final_day_index) {
  CHECK_GT(final_day_index, backfill_days_);
  // The earliest day index for which we might need to generate an
  // Observation.
  auto backfill_period_start = uint32_t(final_day_index - backfill_days_);

  for (uint32_t event_code = 0; event_code < num_event_codes; event_code++) {
    auto daily_aggregates =
        report_aggregates.unique_actives_aggregates().by_event_code().find(event_code);
    // Have any events ever been logged for this report and event code?
    bool found_event_code =
        (daily_aggregates != report_aggregates.unique_actives_aggregates().by_event_code().end());
    for (const auto& window : report_aggregates.aggregation_config().aggregation_window()) {
      // Skip all hourly windows, and all daily windows which are larger than
      // kMaxAllowedAggregationDays.
      //
      // TODO(pesk): Generate observations for hourly windows.
      if (window.units_case() != OnDeviceAggregationWindow::kDays) {
        LOG(INFO) << "Skipping unsupported aggregation window.";
        continue;
      }
      if (window.days() > kMaxAllowedAggregationDays) {
        LOG(WARNING) << "GenerateUniqueActivesObservations ignoring a window "
                        "size exceeding the maximum allowed value";
        continue;
      }
      // Find the earliest day index for which an Observation has not yet
      // been generated for this report, event code, and window size. If
      // that day index is later than |final_day_index|, no Observation is
      // generated on this invocation.
      auto last_gen = GetUniqueActivesLastGeneratedDayIndex(report_key, event_code, window.days());
      auto first_day_index = std::max(last_gen + 1, backfill_period_start);
      // The latest day index on which |event_type| is known to have
      // occurred, so far. This value will be updated as we search
      // forward from the earliest day index belonging to a window of
      // interest.
      uint32_t active_day_index = 0u;
      // Iterate over the day indices |obs_day_index| for which we need
      // to generate Observations. On each iteration, generate an
      // Observation for the |window| ending on |obs_day_index|.
      for (uint32_t obs_day_index = first_day_index; obs_day_index <= final_day_index;
           obs_day_index++) {
        bool was_active = false;
        if (found_event_code) {
          // If the current value of |active_day_index| falls within the
          // window, generate an Observation of activity. If not, search
          // forward in the window, update |active_day_index|, and generate an
          // Observation of activity or inactivity depending on the result of
          // the search.
          if (IsActivityInWindow(active_day_index, obs_day_index, window.days())) {
            was_active = true;
          } else {
            active_day_index =
                FirstActiveDayIndexInWindow(daily_aggregates->second, obs_day_index, window.days());
            was_active = IsActivityInWindow(active_day_index, obs_day_index, window.days());
          }
        }
        auto status = GenerateSingleUniqueActivesObservation(
            metric_ref, &report_aggregates.aggregation_config().report(), obs_day_index, event_code,
            window, was_active);
        if (status != kOK) {
          return status;
        }

        SetUniqueActivesLastGeneratedDayIndex(report_key, event_code, window.days(), obs_day_index);
      }
    }
  }
  return kOK;
}

////////// GenerateObsFromNumericAggregates and helper methods /////////////

uint32_t AggregateStore::GetPerDeviceNumericLastGeneratedDayIndex(const std::string& report_key,
                                                                  const std::string& component,
                                                                  uint32_t event_code,
                                                                  uint32_t aggregation_days) const {
  auto obs_history = protected_obs_history_.const_lock()->obs_history;
  const auto& report_history = obs_history.by_report_key().find(report_key);
  if (report_history == obs_history.by_report_key().end()) {
    return 0u;
  }
  if (!report_history->second.has_per_device_numeric_history()) {
    return 0u;
  }
  const auto& component_history =
      report_history->second.per_device_numeric_history().by_component().find(component);
  if (component_history ==
      report_history->second.per_device_numeric_history().by_component().end()) {
    return 0u;
  }
  const auto& event_code_history = component_history->second.by_event_code().find(event_code);
  if (event_code_history == component_history->second.by_event_code().end()) {
    return 0u;
  }
  const auto& window_history = event_code_history->second.by_window_size().find(aggregation_days);
  if (window_history == event_code_history->second.by_window_size().end()) {
    return 0u;
  }
  return window_history->second;
}

void AggregateStore::SetPerDeviceNumericLastGeneratedDayIndex(const std::string& report_key,
                                                              const std::string& component,
                                                              uint32_t event_code,
                                                              uint32_t aggregation_days,
                                                              uint32_t value) {
  auto locked = protected_obs_history_.lock();
  (*(*(*(*locked->obs_history.mutable_by_report_key())[report_key]
            .mutable_per_device_numeric_history()
            ->mutable_by_component())[component]
          .mutable_by_event_code())[event_code]
        .mutable_by_window_size())[aggregation_days] = value;
}

uint32_t AggregateStore::GetReportParticipationLastGeneratedDayIndex(
    const std::string& report_key) const {
  auto obs_history = protected_obs_history_.const_lock()->obs_history;
  const auto& report_history = obs_history.by_report_key().find(report_key);
  if (report_history == obs_history.by_report_key().end()) {
    return 0u;
  }
  return report_history->second.report_participation_history().last_generated();
}

void AggregateStore::SetReportParticipationLastGeneratedDayIndex(const std::string& report_key,
                                                                 uint32_t value) {
  auto locked = protected_obs_history_.lock();
  (*locked->obs_history.mutable_by_report_key())[report_key]
      .mutable_report_participation_history()
      ->set_last_generated(value);
}

void AggregateStore::DeleteData() {
  LOG(INFO) << "AggregateStore: Deleting stored data";

  {
    auto locked = protected_aggregate_store_.lock();
    locked->local_aggregate_store = locked->empty_local_aggregate_store;
  }
  protected_obs_history_.lock()->obs_history = MakeNewObservationHistoryStore();
}

void AggregateStore::Disable(bool is_disabled) {
  LOG(INFO) << "AggregateStore: " << (is_disabled ? "Disabling" : "Enabling")
            << " event aggregate storage.";
  is_disabled_ = is_disabled;
}

Status AggregateStore::GenerateSinglePerDeviceNumericObservation(
    const MetricRef metric_ref, const ReportDefinition* report, uint32_t obs_day_index,
    const std::string& component, uint32_t event_code, const OnDeviceAggregationWindow& window,
    int64_t value) const {
  Encoder::Result encoder_result =
      encoder_->EncodePerDeviceNumericObservation(metric_ref, report, obs_day_index, component,
                                                  UnpackEventCodesProto(event_code), value, window);
  if (encoder_result.status != kOK) {
    return encoder_result.status;
  }
  if (encoder_result.observation == nullptr || encoder_result.metadata == nullptr) {
    LOG(ERROR) << "Failed to encode PerDeviceNumericObservation";
    return kOther;
  }

  const auto& writer_status = observation_writer_->WriteObservation(
      *encoder_result.observation, std::move(encoder_result.metadata));
  if (writer_status != kOK) {
    return writer_status;
  }
  return kOK;
}

Status AggregateStore::GenerateSinglePerDeviceHistogramObservation(
    const MetricRef metric_ref, const ReportDefinition* report, uint32_t obs_day_index,
    const std::string& component, uint32_t event_code, const OnDeviceAggregationWindow& window,
    int64_t value) const {
  Encoder::Result encoder_result = encoder_->EncodePerDeviceHistogramObservation(
      metric_ref, report, obs_day_index, component, UnpackEventCodesProto(event_code), value,
      window);

  if (encoder_result.status != kOK) {
    return encoder_result.status;
  }
  if (encoder_result.observation == nullptr || encoder_result.metadata == nullptr) {
    LOG(ERROR) << "Failed to encode PerDeviceNumericObservation";
    return kOther;
  }

  const auto& writer_status = observation_writer_->WriteObservation(
      *encoder_result.observation, std::move(encoder_result.metadata));
  if (writer_status != kOK) {
    return writer_status;
  }
  return kOK;
}

Status AggregateStore::GenerateSingleReportParticipationObservation(const MetricRef metric_ref,
                                                                    const ReportDefinition* report,
                                                                    uint32_t obs_day_index) const {
  auto encoder_result =
      encoder_->EncodeReportParticipationObservation(metric_ref, report, obs_day_index);
  if (encoder_result.status != kOK) {
    return encoder_result.status;
  }
  if (encoder_result.observation == nullptr || encoder_result.metadata == nullptr) {
    LOG(ERROR) << "Failed to encode ReportParticipationObservation";
    return kOther;
  }

  const auto& writer_status = observation_writer_->WriteObservation(
      *encoder_result.observation, std::move(encoder_result.metadata));
  if (writer_status != kOK) {
    return writer_status;
  }
  return kOK;
}

Status AggregateStore::GenerateObsFromNumericAggregates(const MetricRef metric_ref,
                                                        const std::string& report_key,
                                                        const ReportAggregates& report_aggregates,
                                                        uint32_t final_day_index) {
  CHECK_GT(final_day_index, backfill_days_);
  // The first day index for which we might have to generate an Observation.
  auto backfill_period_start = uint32_t(final_day_index - backfill_days_);

  // Generate any necessary PerDeviceNumericObservations for this report.
  for (const auto& [component, event_code_aggregates] :
       report_aggregates.numeric_aggregates().by_component()) {
    for (const auto& [event_code, daily_aggregates] : event_code_aggregates.by_event_code()) {
      // Populate a helper map keyed by day indices which belong to the range
      // [|backfill_period_start|, |final_day_index|]. The value at a day
      // index is the list of windows, in increasing size order, for which an
      // Observation should be generated for that day index.
      std::map<uint32_t, std::vector<OnDeviceAggregationWindow>> windows_by_obs_day;
      for (const auto& window : report_aggregates.aggregation_config().aggregation_window()) {
        if (window.units_case() != OnDeviceAggregationWindow::kDays) {
          LOG(INFO) << "Skipping unsupported aggregation window.";
          continue;
        }
        auto last_gen = GetPerDeviceNumericLastGeneratedDayIndex(report_key, component, event_code,
                                                                 window.days());
        auto first_day_index = std::max(last_gen + 1, backfill_period_start);
        for (auto obs_day_index = first_day_index; obs_day_index <= final_day_index;
             obs_day_index++) {
          windows_by_obs_day[obs_day_index].push_back(window);
        }
      }
      // Iterate over the day indices |obs_day_index| for which we might need
      // to generate an Observation. For each day index, generate an
      // Observation for each needed window.
      //
      // More precisely, for each needed window, iterate over the days within that window. If at
      // least one numeric event was logged during the window, compute the aggregate of the numeric
      // values over the window and generate a PerDeviceNumericObservation. Whether or not a numeric
      // event was found, update the AggregatedObservationHistory for this report, component, event
      // code, and window size with |obs_day_index| as the most recent date of Observation
      // generation. This reflects the fact that all needed Observations were generated for the
      // window ending on that date.
      for (auto obs_day_index = backfill_period_start; obs_day_index <= final_day_index;
           obs_day_index++) {
        const auto& windows = windows_by_obs_day.find(obs_day_index);
        if (windows == windows_by_obs_day.end()) {
          continue;
        }
        bool found_value_for_window = false;
        int64_t window_aggregate = 0;
        uint32_t num_days = 0;
        for (const auto& window : windows->second) {
          while (num_days < window.days()) {
            bool found_value_for_day = false;
            const auto& day_aggregates =
                daily_aggregates.by_day_index().find(obs_day_index - num_days);
            if (day_aggregates != daily_aggregates.by_day_index().end()) {
              found_value_for_day = true;
            }
            const auto& aggregation_type =
                report_aggregates.aggregation_config().report().aggregation_type();
            switch (aggregation_type) {
              case ReportDefinition::SUM:
                if (found_value_for_day) {
                  window_aggregate += day_aggregates->second.numeric_daily_aggregate().value();
                  found_value_for_window = true;
                }
                break;
              case ReportDefinition::MAX:
                if (found_value_for_day) {
                  window_aggregate = std::max(
                      window_aggregate, day_aggregates->second.numeric_daily_aggregate().value());
                  found_value_for_window = true;
                }
                break;
              case ReportDefinition::MIN:
                if (found_value_for_day && !found_value_for_window) {
                  window_aggregate = day_aggregates->second.numeric_daily_aggregate().value();
                  found_value_for_window = true;
                } else if (found_value_for_day) {
                  window_aggregate = std::min(
                      window_aggregate, day_aggregates->second.numeric_daily_aggregate().value());
                }
                break;
              default:
                LOG(ERROR) << "Unexpected aggregation type " << aggregation_type;
                return kInvalidArguments;
            }
            num_days++;
          }
          if (found_value_for_window) {
            Status status;
            const ReportDefinition* report = &report_aggregates.aggregation_config().report();
            switch (report->report_type()) {
              case ReportDefinition::PER_DEVICE_NUMERIC_STATS: {
                status = GenerateSinglePerDeviceNumericObservation(
                    metric_ref, report, obs_day_index, component, event_code, window,
                    window_aggregate);
                if (status != kOK) {
                  return status;
                }
                break;
              }
              case ReportDefinition::PER_DEVICE_HISTOGRAM: {
                auto status = GenerateSinglePerDeviceHistogramObservation(
                    metric_ref, report, obs_day_index, component, event_code, window,
                    window_aggregate);
                if (status != kOK) {
                  return status;
                }
                break;
              }
              default:
                LOG(ERROR) << "Unexpected report type " << report->report_type();
                return kInvalidArguments;
            }
          }

          SetPerDeviceNumericLastGeneratedDayIndex(report_key, component, event_code, window.days(),
                                                   obs_day_index);
        }
      }
    }
  }
  // Generate any necessary ReportParticipationObservations for this report.
  auto participation_last_gen = GetReportParticipationLastGeneratedDayIndex(report_key);
  auto participation_first_day_index = std::max(participation_last_gen + 1, backfill_period_start);
  for (auto obs_day_index = participation_first_day_index; obs_day_index <= final_day_index;
       obs_day_index++) {
    GenerateSingleReportParticipationObservation(
        metric_ref, &report_aggregates.aggregation_config().report(), obs_day_index);
    SetReportParticipationLastGeneratedDayIndex(report_key, obs_day_index);
  }
  return kOK;
}

LocalAggregateStore AggregateStore::MakeNewLocalAggregateStore(uint32_t version) {
  LocalAggregateStore store;
  store.set_version(version);
  return store;
}

AggregatedObservationHistoryStore AggregateStore::MakeNewObservationHistoryStore(uint32_t version) {
  AggregatedObservationHistoryStore store;
  store.set_version(version);
  return store;
}

// We can upgrade from v0, but no other versions.
Status AggregateStore::MaybeUpgradeLocalAggregateStore(LocalAggregateStore* store) {
  uint32_t version = store->version();
  if (version == kCurrentLocalAggregateStoreVersion) {
    return kOK;
  }
  VLOG(4) << "Attempting to upgrade LocalAggregateStore from version " << version;
  switch (version) {
    case 0u:
      return UpgradeLocalAggregateStoreFromVersion0(store);
    default:
      LOG(ERROR) << "Cannot upgrade LocalAggregateStore from version " << version;
      return kInvalidArguments;
  }
}

// The current version is the earliest version, so no other versions are accepted.
Status AggregateStore::MaybeUpgradeObservationHistoryStore(
    AggregatedObservationHistoryStore* store) {
  uint32_t version = store->version();
  if (version == kCurrentObservationHistoryStoreVersion) {
    return kOK;
  }
  LOG(ERROR) << "Cannot upgrade AggregatedObservationHistoryStore from version " << version;
  return kInvalidArguments;
}

}  // namespace cobalt::local_aggregation

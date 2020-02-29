// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/logger/fake_logger.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "src/lib/util/status.h"
#include "src/logger/logger_interface.h"
#include "src/registry/project_configs.h"

namespace cobalt::logger::testing {

namespace {

template <class EventType>
void CopyEventCodesAndComponent(const std::vector<uint32_t>& event_codes,
                                const std::string& component, EventType* event) {
  for (auto event_code : event_codes) {
    event->add_event_code(event_code);
  }
  event->set_component(component);
}

template <class EventType>
void CopyEventCodes(const std::vector<uint32_t>& event_codes, EventType* event) {
  for (auto event_code : event_codes) {
    event->add_event_code(event_code);
  }
}

}  // namespace

Status FakeLogger::LogEvent(uint32_t metric_id, uint32_t event_code) {
  call_count_ += 1;

  Event event;
  event.set_metric_id(metric_id);
  event.mutable_event_occurred_event()->set_event_code(event_code);
  last_event_logged_ = event;

  return Status::kOK;
}

Status FakeLogger::LogEventCount(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                                 const std::string& component, int64_t period_duration_micros,
                                 uint32_t count) {
  call_count_ += 1;

  Event event;
  event.set_metric_id(metric_id);
  auto* event_count_event = event.mutable_event_count_event();
  CopyEventCodesAndComponent(event_codes, component, event_count_event);
  event_count_event->set_period_duration_micros(period_duration_micros);
  event_count_event->set_count(count);
  last_event_logged_ = event;

  return Status::kOK;
}

Status FakeLogger::LogElapsedTime(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                                  const std::string& component, int64_t elapsed_micros) {
  call_count_ += 1;

  Event event;
  event.set_metric_id(metric_id);
  auto* elapsed_time_event = event.mutable_elapsed_time_event();
  CopyEventCodesAndComponent(event_codes, component, elapsed_time_event);
  elapsed_time_event->set_elapsed_micros(elapsed_micros);
  last_event_logged_ = event;

  return Status::kOK;
}

Status FakeLogger::LogFrameRate(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                                const std::string& component, float fps) {
  call_count_ += 1;

  Event event;
  event.set_metric_id(metric_id);
  auto* frame_rate_event = event.mutable_frame_rate_event();
  CopyEventCodesAndComponent(event_codes, component, frame_rate_event);
  // NOLINTNEXTLINE readability-magic-numbers
  frame_rate_event->set_frames_per_1000_seconds(std::round(fps * 1000.0));
  last_event_logged_ = event;

  return Status::kOK;
}

Status FakeLogger::LogMemoryUsage(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                                  const std::string& component, int64_t bytes) {
  call_count_ += 1;

  Event event;
  event.set_metric_id(metric_id);
  auto* memory_usage_event = event.mutable_memory_usage_event();
  CopyEventCodesAndComponent(event_codes, component, memory_usage_event);
  memory_usage_event->set_bytes(bytes);
  last_event_logged_ = event;

  return Status::kOK;
}

Status FakeLogger::LogIntHistogram(uint32_t metric_id, const std::vector<uint32_t>& event_codes,
                                   const std::string& component, HistogramPtr histogram) {
  call_count_ += 1;

  Event event;
  event.set_metric_id(metric_id);
  auto* int_histogram_event = event.mutable_int_histogram_event();
  CopyEventCodesAndComponent(event_codes, component, int_histogram_event);
  int_histogram_event->mutable_buckets()->Swap(histogram.get());
  last_event_logged_ = event;

  return Status::kOK;
}

Status FakeLogger::LogOccurrence(uint32_t metric_id, uint64_t count,
                                 const std::vector<uint32_t>& event_codes) {
  call_count_ += 1;

  Event event;
  event.set_metric_id(metric_id);
  auto* occurrence_event = event.mutable_occurrence_event();
  CopyEventCodes(event_codes, occurrence_event);
  occurrence_event->set_count(count);
  last_event_logged_ = event;

  return Status::kOK;
}

Status FakeLogger::LogInteger(uint32_t metric_id, int64_t value,
                              const std::vector<uint32_t>& event_codes) {
  call_count_ += 1;

  Event event;
  event.set_metric_id(metric_id);
  auto* integer_event = event.mutable_integer_event();
  CopyEventCodes(event_codes, integer_event);
  integer_event->set_value(value);
  last_event_logged_ = event;

  return Status::kOK;
}

Status FakeLogger::LogIntegerHistogram(uint32_t metric_id, HistogramPtr histogram,
                                       const std::vector<uint32_t>& event_codes) {
  call_count_ += 1;

  Event event;
  event.set_metric_id(metric_id);
  auto* integer_histogram_event = event.mutable_integer_histogram_event();
  CopyEventCodes(event_codes, integer_histogram_event);
  integer_histogram_event->mutable_buckets()->Swap(histogram.get());
  last_event_logged_ = event;

  return Status::kOK;
}

Status FakeLogger::LogString(uint32_t metric_id, const std::string& string_value,
                             const std::vector<uint32_t>& event_codes) {
  call_count_ += 1;

  Event event;
  event.set_metric_id(metric_id);
  auto* string_event = event.mutable_string_event();
  CopyEventCodes(event_codes, string_event);
  string_event->set_string_value(string_value);
  last_event_logged_ = event;

  return Status::kOK;
}

Status FakeLogger::LogCustomEvent(uint32_t metric_id, EventValuesPtr event_values) {
  call_count_ += 1;

  Event event;
  event.set_metric_id(metric_id);
  event.mutable_custom_event()->mutable_values()->swap(*event_values);
  last_event_logged_ = event;

  return Status::kOK;
}

}  // namespace cobalt::logger::testing

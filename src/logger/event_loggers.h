// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LOGGER_EVENT_LOGGERS_H_
#define COBALT_SRC_LOGGER_EVENT_LOGGERS_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/local_aggregation/event_aggregator.h"
#include "src/logger/encoder.h"
#include "src/logger/event_record.h"
#include "src/logger/observation_writer.h"
#include "src/logger/project_context.h"
#include "src/logger/status.h"
#include "src/logging.h"
#include "src/pb/event.pb.h"
#include "src/pb/observation2.pb.h"
#include "src/registry/metric_definition.pb.h"
#include "src/registry/report_definition.pb.h"
#include "src/tracing.h"

namespace cobalt::logger::internal {

using ::google::protobuf::RepeatedField;

// EventLogger is an abstract interface used internally in logger.cc to
// dispatch logging logic based on Metric type. Below we create subclasses
// of EventLogger for each of several Metric types.
class EventLogger {
 public:
  EventLogger(const Encoder* encoder, local_aggregation::EventAggregator* event_aggregator,
              const ObservationWriter* observation_writer,
              const system_data::SystemDataInterface* system_data)
      : encoder_(encoder),
        event_aggregator_(event_aggregator),
        observation_writer_(observation_writer),
        system_data_(system_data) {}

  virtual ~EventLogger() = default;

  // Factory for creating an appropriate EventLogger subclass for the type of metric being logged.
  //
  // |metric_type| is the type of metric to be logged with the created event logger.
  //
  // The remaining parameters are passed to the EventLogger constructor.
  static std::unique_ptr<EventLogger> Create(MetricDefinition::MetricType metric_type,
                                             const Encoder* encoder,
                                             local_aggregation::EventAggregator* event_aggregator,
                                             const ObservationWriter* observation_writer,
                                             const system_data::SystemDataInterface* system_data);

  // Finds the Metric with the given ID. Expects that this has type
  // |expected_metric_type|. If not logs an error and returns.
  // If so then logs the Event specified by |event_record| to Cobalt.
  // The |event_timestamp| is recorded as the time the event occurred at.
  Status Log(std::unique_ptr<EventRecord> event_record,
             const std::chrono::system_clock::time_point& event_timestamp);

  // Prepare an event for logging, and validate that it is suitable.
  Status PrepareAndValidateEvent(uint32_t metric_id, MetricDefinition::MetricType expected_type,
                                 EventRecord* event_record);

 protected:
  const Encoder* encoder() { return encoder_; }
  local_aggregation::EventAggregator* event_aggregator() { return event_aggregator_; }

  Encoder::Result BadReportType(const std::string& full_metric_name,
                                const ReportDefinition& report);

  // Validates the supplied event_codes against the defined metric dimensions
  // in the MetricDefinition.
  virtual Status ValidateEventCodes(const MetricDefinition& metric,
                                    const RepeatedField<uint32_t>& event_codes,
                                    const std::string& full_metric_name);

 private:
  friend class EventLoggersAddEventTest;

  // Validate that the event is suitable for logging.
  //
  // Most of the event validation should be done here, as this occurs at the time
  // the event occurs.
  virtual Status ValidateEvent(const EventRecord& event_record);

  // Finishes setting up the event using |event_timestamp| as the time the event occurred at,
  // and then performs any final validation of the |event_record|.
  void FinalizeEvent(EventRecord* event_record,
                     const std::chrono::system_clock::time_point& event_timestamp);

  // Given an EventRecord and a ReportDefinition, determines whether or not
  // the Event should be used to update a local aggregation and if so passes
  // the Event to the Local Aggregator.
  virtual Status MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                             const EventRecord& event_record);

  // Given an EventRecord and a ReportDefinition, determines whether or not
  // the Event should be used to generate an immediate Observation and if so
  // does generate one and writes it to the Observation Store.
  //
  // |may_invalidate| indicates that the implementation is allowed to invalidate
  // |event_record|. This should be set true only when it is known that
  // |event_record| is no longer needed. Setting this true allows the data in
  // |event_record| to be moved rather than copied.
  Status MaybeGenerateImmediateObservation(const ReportDefinition& report, bool may_invalidate,
                                           EventRecord* event_record);

  // Given an EventRecord and a ReportDefinition, determines whether or not
  // the Event should be used to generate an immediate Observation and if so
  // does generate one. This method is invoked by
  // MaybeGenerateImmediateObservation().
  //
  // |may_invalidate| indicates that the implementation is allowed to invalidate
  // |event_record|. This should be set true only when it is known that
  // |event_record| is no longer needed. Setting this true allows the data in
  // |event_record| to be moved rather than copied.
  virtual Encoder::Result MaybeEncodeImmediateObservation(const ReportDefinition& report,
                                                          bool may_invalidate,
                                                          EventRecord* event_record);

  // Traces an |event_record| into a string if the metric is tagged with
  // also_log_locally. If not, it will return the empty string.
  std::string TraceEvent(const EventRecord& event_record);

  // Logs a trace of an |event_record| that failed to be logged to Cobalt.
  //
  // |status| The status code reported to the user.
  // |event_record| The event_record associated with the event.
  // |trace| The string output from TraceEvent().
  // |report| Information about the report for which the logging failed.
  void TraceLogFailure(const Status& status, const EventRecord& event_record,
                       const std::string& trace, const ReportDefinition& report);

  // Logs a trace of an |event_record| that successfully logged to Cobalt.
  //
  // |event_record| The event_record associated with the event.
  // |trace| The string output from TraceEvent().
  void TraceLogSuccess(const EventRecord& event_record, const std::string& trace);

  const Encoder* encoder_;
  local_aggregation::EventAggregator* event_aggregator_;
  const ObservationWriter* observation_writer_;
  const system_data::SystemDataInterface* system_data_;
};

// Implementation of EventLogger for metrics of type EVENT_OCCURRED.
class EventOccurredEventLogger : public EventLogger {
 public:
  using EventLogger::EventLogger;
  ~EventOccurredEventLogger() override = default;

 private:
  Status ValidateEvent(const EventRecord& event_record) override;
  Encoder::Result MaybeEncodeImmediateObservation(const ReportDefinition& report,
                                                  bool may_invalidate,
                                                  EventRecord* event_record) override;
  Status MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                     const EventRecord& event_record) override;
};

// Implementation of EventLogger for metrics of type EVENT_COUNT.
class EventCountEventLogger : public EventLogger {
 public:
  using EventLogger::EventLogger;
  ~EventCountEventLogger() override = default;

 private:
  Status ValidateEvent(const EventRecord& event_record) override;
  Encoder::Result MaybeEncodeImmediateObservation(const ReportDefinition& report,
                                                  bool may_invalidate,
                                                  EventRecord* event_record) override;
  Status MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                     const EventRecord& event_record) override;
};

// Implementation of EventLogger for all of the numerical performance metric
// types. This is an abstract class. There are subclasses below for each
// metric type.
class IntegerPerformanceEventLogger : public EventLogger {
 protected:
  using EventLogger::EventLogger;
  ~IntegerPerformanceEventLogger() override = default;

 private:
  Encoder::Result MaybeEncodeImmediateObservation(const ReportDefinition& report,
                                                  bool may_invalidate,
                                                  EventRecord* event_record) override;
  virtual const RepeatedField<uint32_t>& EventCodes(const Event& event) = 0;
  virtual std::string Component(const Event& event) = 0;
  virtual int64_t IntValue(const Event& event) = 0;
};

// Implementation of EventLogger for metrics of type ELAPSED_TIME.
class ElapsedTimeEventLogger : public IntegerPerformanceEventLogger {
 public:
  using IntegerPerformanceEventLogger::IntegerPerformanceEventLogger;
  ~ElapsedTimeEventLogger() override = default;

 private:
  const RepeatedField<uint32_t>& EventCodes(const Event& event) override;
  std::string Component(const Event& event) override;
  int64_t IntValue(const Event& event) override;
  Status ValidateEvent(const EventRecord& event_record) override;
  Status MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                     const EventRecord& event_record) override;
};

// Implementation of EventLogger for metrics of type FRAME_RATE.
class FrameRateEventLogger : public IntegerPerformanceEventLogger {
 public:
  using IntegerPerformanceEventLogger::IntegerPerformanceEventLogger;
  ~FrameRateEventLogger() override = default;

 private:
  const RepeatedField<uint32_t>& EventCodes(const Event& event) override;
  std::string Component(const Event& event) override;
  int64_t IntValue(const Event& event) override;
  Status ValidateEvent(const EventRecord& event_record) override;
  Status MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                     const EventRecord& event_record) override;
};

// Implementation of EventLogger for metrics of type MEMORY_USAGE.
class MemoryUsageEventLogger : public IntegerPerformanceEventLogger {
 public:
  using IntegerPerformanceEventLogger::IntegerPerformanceEventLogger;
  ~MemoryUsageEventLogger() override = default;

 private:
  const RepeatedField<uint32_t>& EventCodes(const Event& event) override;
  std::string Component(const Event& event) override;
  int64_t IntValue(const Event& event) override;
  Status ValidateEvent(const EventRecord& event_record) override;
  Status MaybeUpdateLocalAggregation(const ReportDefinition& report,
                                     const EventRecord& event_record) override;
};

// Implementation of EventLogger for metrics of type INT_HISTOGRAM.
class IntHistogramEventLogger : public EventLogger {
 public:
  using EventLogger::EventLogger;
  ~IntHistogramEventLogger() override = default;

 private:
  Status ValidateEvent(const EventRecord& event_record) override;
  Encoder::Result MaybeEncodeImmediateObservation(const ReportDefinition& report,
                                                  bool may_invalidate,
                                                  EventRecord* event_record) override;
};

// Implementation of EventLogger for metrics of type CUSTOM.
class CustomEventLogger : public EventLogger {
 public:
  using EventLogger::EventLogger;
  ~CustomEventLogger() override = default;

 private:
  Status ValidateEvent(const EventRecord& event_record) override;
  Encoder::Result MaybeEncodeImmediateObservation(const ReportDefinition& report,
                                                  bool may_invalidate,
                                                  EventRecord* event_record) override;
};

}  // namespace cobalt::logger::internal

#endif  // COBALT_SRC_LOGGER_EVENT_LOGGERS_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/logger/internal_metrics.h"

#include <vector>

#include "src/gtest.h"
#include "src/logger/fake_logger.h"

namespace cobalt {
namespace logger {

constexpr int64_t kNumBytes = 123;
constexpr uint32_t kCustomerId = 1;
constexpr uint32_t kProjectId = 2;
constexpr uint32_t kMany = 100;

class InternalMetricsImplTest : public ::testing::Test {
 public:
  Project GetTestProject() {
    Project project;
    project.set_customer_id(kCustomerId);
    project.set_customer_name("test");
    project.set_project_id(kProjectId);
    project.set_project_name("project");
    return project;
  }
};

TEST_F(InternalMetricsImplTest, LoggerCalled) {
  testing::FakeLogger logger;
  InternalMetricsImpl metrics(&logger);

  metrics.LoggerCalled(LoggerCallsMadeMetricDimensionLoggerMethod::LogMemoryUsage,
                       GetTestProject());

  ASSERT_EQ(logger.call_count(), 2);
  ASSERT_TRUE(logger.last_event_logged().has_count_event());
  ASSERT_EQ(logger.last_event_logged().count_event().component(), "test/project");
}

TEST_F(InternalMetricsImplTest, LoggerCalledPauseWorks) {
  testing::FakeLogger logger;
  InternalMetricsImpl metrics(&logger);

  metrics.PauseLogging();
  for (int i = 0; i < kMany; i++) {
    metrics.LoggerCalled(LoggerCallsMadeMetricDimensionLoggerMethod::LogMemoryUsage,
                         GetTestProject());
  }
  metrics.ResumeLogging();

  ASSERT_EQ(logger.call_count(), 0);
}

TEST_F(InternalMetricsImplTest, BytesUploaded) {
  testing::FakeLogger logger;
  InternalMetricsImpl metrics(&logger);

  ASSERT_EQ(logger.call_count(), 0);
  metrics.BytesUploaded(PerDeviceBytesUploadedMetricDimensionStatus::Attempted, kNumBytes);

  ASSERT_EQ(logger.call_count(), 1);
  ASSERT_TRUE(logger.last_event_logged().has_count_event());
  ASSERT_EQ(logger.last_event_logged().count_event().count(), kNumBytes);
}

TEST_F(InternalMetricsImplTest, BytesUploadedPauseWorks) {
  testing::FakeLogger logger;
  InternalMetricsImpl metrics(&logger);

  metrics.PauseLogging();
  for (int i = 0; i < kMany; i++) {
    metrics.BytesUploaded(PerDeviceBytesUploadedMetricDimensionStatus::Attempted, kNumBytes);
  }
  metrics.ResumeLogging();
  ASSERT_EQ(logger.call_count(), 0);
}

TEST_F(InternalMetricsImplTest, BytesStored) {
  testing::FakeLogger logger;
  InternalMetricsImpl metrics(&logger);

  ASSERT_EQ(logger.call_count(), 0);
  metrics.BytesStored(PerProjectBytesStoredMetricDimensionStatus::Attempted, kNumBytes, kCustomerId,
                      kProjectId);

  ASSERT_EQ(logger.call_count(), 1);
  ASSERT_TRUE(logger.last_event_logged().has_memory_usage_event());
  ASSERT_EQ(logger.last_event_logged().memory_usage_event().bytes(), kNumBytes);
}

TEST_F(InternalMetricsImplTest, BytesStoredPauseWorks) {
  testing::FakeLogger logger;
  InternalMetricsImpl metrics(&logger);

  metrics.PauseLogging();
  for (int i = 0; i < kMany; i++) {
    metrics.BytesStored(PerProjectBytesStoredMetricDimensionStatus::Attempted, kNumBytes,
                        kCustomerId, kProjectId);
  }
  metrics.ResumeLogging();
  ASSERT_EQ(logger.call_count(), 0);
}

}  // namespace logger
}  // namespace cobalt

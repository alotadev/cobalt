// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/algorithms/rappor/rappor_config_helper.h"

#include "src/logging.h"
#include "src/registry/metric_definition.pb.h"
#include "src/registry/report_definition.pb.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt::rappor {

TEST(RapporConfigHelperTest, ProbBitFlip) {
  ReportDefinition report_definition;
  EXPECT_EQ(RapporConfigHelper::kInvalidProbability,
            RapporConfigHelper::ProbBitFlip(report_definition, "my.test.metric"));

  report_definition.set_local_privacy_noise_level(ReportDefinition::NONE);
  EXPECT_EQ(0.0f, RapporConfigHelper::ProbBitFlip(report_definition, "my.test.metric"));

  report_definition.set_local_privacy_noise_level(ReportDefinition::SMALL);
  EXPECT_EQ(0.01f, RapporConfigHelper::ProbBitFlip(report_definition, "my.test.metric"));

  report_definition.set_local_privacy_noise_level(ReportDefinition::MEDIUM);
  EXPECT_EQ(0.1f, RapporConfigHelper::ProbBitFlip(report_definition, "my.test.metric"));

  report_definition.set_local_privacy_noise_level(ReportDefinition::LARGE);
  EXPECT_EQ(0.25f, RapporConfigHelper::ProbBitFlip(report_definition, "my.test.metric"));
}

TEST(RapporConfigHelperTest, BasicRapporNumCategories) {
  MetricDefinition metric_definition;
  EXPECT_EQ(0u, RapporConfigHelper::BasicRapporNumCategories(metric_definition));

  metric_definition.add_metric_dimensions()->set_max_event_code(0);
  EXPECT_EQ(1u, RapporConfigHelper::BasicRapporNumCategories(metric_definition));

  // NOLINTNEXTLINE readability-magic-numbers
  metric_definition.mutable_metric_dimensions(0)->set_max_event_code(10);
  EXPECT_EQ(11u, RapporConfigHelper::BasicRapporNumCategories(metric_definition));

  // NOLINTNEXTLINE readability-magic-numbers
  metric_definition.add_metric_dimensions()->set_max_event_code(10);
  EXPECT_EQ(0u, RapporConfigHelper::BasicRapporNumCategories(metric_definition));
}

}  // namespace cobalt::rappor

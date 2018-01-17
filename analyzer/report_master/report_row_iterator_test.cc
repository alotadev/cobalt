// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "analyzer/report_master/report_row_iterator.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gflags/gflags.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace analyzer {

namespace {

ReportRow MakeRow(const std::string value) {
  ReportRow report_row;
  HistogramReportRow* row = report_row.mutable_histogram();
  row->mutable_value()->set_string_value(value);
  return report_row;
}

}  // namespace

// Tests ReportRowVectorIterator with an empty vector
TEST(ReportRowVectorIteratorTest, EmptyVector) {
  // Make an empty vector
  std::vector<ReportRow> report_rows;

  // Make a ReportRowVectorIterator to wrap it.
  ReportRowVectorIterator iter(&report_rows);

  // Test it.
  EXPECT_EQ(grpc::OK, iter.Reset().error_code());
  EXPECT_EQ(grpc::INVALID_ARGUMENT, iter.NextRow(nullptr).error_code());
  bool has_more_rows;
  ASSERT_TRUE(iter.HasMoreRows(&has_more_rows).ok());
  EXPECT_FALSE(has_more_rows);
  const ReportRow* next_row;
  EXPECT_EQ(grpc::NOT_FOUND, iter.NextRow(&next_row).error_code());
}

// Tests ReportRowVectorIterator with a vector of size 1.
TEST(ReportRowVectorIteratorTest, SizeOne) {
  // Make a vector of length 1
  std::vector<ReportRow> report_rows;
  report_rows.push_back(MakeRow("apple"));

  // Make a ReportRowVectorIterator to wrap it.
  ReportRowVectorIterator iter(&report_rows);

  // Test it.
  const ReportRow* next_row;
  bool has_more_rows;
  ASSERT_TRUE(iter.HasMoreRows(&has_more_rows).ok());
  EXPECT_TRUE(has_more_rows);
  EXPECT_EQ(grpc::OK, iter.NextRow(&next_row).error_code());
  ASSERT_NE(nullptr, next_row);
  EXPECT_EQ("apple", next_row->histogram().value().string_value());
  ASSERT_TRUE(iter.HasMoreRows(&has_more_rows).ok());
  EXPECT_FALSE(has_more_rows);
  EXPECT_EQ(grpc::NOT_FOUND, iter.NextRow(&next_row).error_code());
  EXPECT_EQ(grpc::OK, iter.Reset().error_code());
  ASSERT_TRUE(iter.HasMoreRows(&has_more_rows).ok());
  EXPECT_TRUE(has_more_rows);
  EXPECT_EQ(grpc::OK, iter.NextRow(&next_row).error_code());
  ASSERT_NE(nullptr, next_row);
  EXPECT_EQ("apple", next_row->histogram().value().string_value());
}

// Tests ReportRowVectorIterator with a vector of size 3.
TEST(ReportRowVectorIteratorTest, SizeThree) {
  // Make a vector of length 3
  std::vector<ReportRow> report_rows;
  report_rows.push_back(MakeRow("apple"));
  report_rows.push_back(MakeRow("banana"));
  report_rows.push_back(MakeRow("cantaloupe"));

  // Make a ReportRowVectorIterator to wrap it.
  ReportRowVectorIterator iter(&report_rows);

  // Test it.
  const ReportRow* next_row;
  bool has_more_rows;
  ASSERT_TRUE(iter.HasMoreRows(&has_more_rows).ok());
  EXPECT_TRUE(has_more_rows);
  EXPECT_EQ(grpc::OK, iter.NextRow(&next_row).error_code());
  EXPECT_EQ("apple", next_row->histogram().value().string_value());
  ASSERT_TRUE(iter.HasMoreRows(&has_more_rows).ok());
  EXPECT_TRUE(has_more_rows);
  EXPECT_EQ(grpc::OK, iter.NextRow(&next_row).error_code());
  EXPECT_EQ("banana", next_row->histogram().value().string_value());
  ASSERT_TRUE(iter.HasMoreRows(&has_more_rows).ok());
  EXPECT_TRUE(has_more_rows);
  EXPECT_EQ(grpc::OK, iter.NextRow(&next_row).error_code());
  EXPECT_EQ("cantaloupe", next_row->histogram().value().string_value());
  ASSERT_TRUE(iter.HasMoreRows(&has_more_rows).ok());
  EXPECT_FALSE(has_more_rows);
  EXPECT_EQ(grpc::NOT_FOUND, iter.NextRow(&next_row).error_code());
  EXPECT_EQ(grpc::OK, iter.Reset().error_code());
  ASSERT_TRUE(iter.HasMoreRows(&has_more_rows).ok());
  EXPECT_TRUE(has_more_rows);
  EXPECT_EQ(grpc::OK, iter.NextRow(&next_row).error_code());
  EXPECT_EQ("apple", next_row->histogram().value().string_value());
}

}  // namespace analyzer
}  // namespace cobalt

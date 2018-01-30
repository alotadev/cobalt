// Copyright 2017 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "analyzer/store/bigtable_store.h"

#include <glog/logging.h>
#include <google/bigtable/v2/data.pb.h>
#include <google/rpc/code.pb.h>

#include <algorithm>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "analyzer/store/bigtable_emulator_helper.h"
#include "analyzer/store/bigtable_flags.h"
#include "analyzer/store/bigtable_names.h"
#include "util/crypto_util/base64.h"
#include "util/log_based_metrics.h"

using google::bigtable::admin::v2::BigtableTableAdmin;
using google::bigtable::admin::v2::DropRowRangeRequest;
using google::bigtable::v2::Bigtable;
using google::bigtable::v2::MutateRowRequest;
using google::bigtable::v2::MutateRowResponse;
using google::bigtable::v2::MutateRowsRequest;
using google::bigtable::v2::MutateRowsResponse;
using google::bigtable::v2::Mutation_SetCell;
using google::bigtable::v2::ReadRowsRequest;
using google::bigtable::v2::ReadRowsResponse;
using google::bigtable::v2::ReadRowsResponse_CellChunk;
using google::bigtable::v2::RowRange;
using google::bigtable::v2::RowSet;
using google::protobuf::Empty;
using grpc::ClientContext;
using grpc::ClientReader;

typedef google::bigtable::admin::v2::Table BtTable;

namespace cobalt {
namespace analyzer {
namespace store {

using crypto::RegexDecode;
using crypto::RegexEncode;

// Stackdriver metric constants
namespace {
const char kReadRowsFailure[] = "bigtable-store-read-rows-failure";
const char kWriteRowsFailure[] = "bigtable-store-write-rows-failure";
const char kDeleteAllRowsFailure[] = "bigtable-store-delete-all-rows-failure";
const char kDeleteRowFailure[] = "bigtable-store-delete-row-failure";
const char kDeleteRowsWithPrefixFailure[] =
    "bigtable-store-delete-rows-with-prefix-failure";
const char kDoWriteRowsFailure[] = "bigtable-store-do-write-rows-failure";
}  // namespace

namespace {
// We never request more than this many rows regardless of how many the user
// asks for. Bigtable fails with "operation aborted", status_code=10 if too
// many rows are requested.
size_t kMaxRowsReadLimit = 10000;

// Returns an error message appropriate for LOG(ERROR) based on the given
// status (which should be an error status) and the name of the method in
// which the error occured.
std::string ErrorMessage(const grpc::Status& status,
                         const std::string& method_name) {
  std::ostringstream stream;
  stream << "Error during " << method_name << ": " << status.error_message()
         << " code=" << status.error_code();
  return stream.str();
}

store::Status GrpcStatusToStoreStatus(const grpc::Status& status) {
  switch (status.error_code()) {
    case grpc::INVALID_ARGUMENT:
      return kInvalidArguments;
    default:
      return kOperationFailed;
  }
}

// Returns whether or not an operation should be retried based on its returned
// status.
bool ShouldRetry(const grpc::Status& status) {
  switch (status.error_code()) {
    case grpc::ABORTED:
    case grpc::CANCELLED:
    case grpc::DEADLINE_EXCEEDED:
    case grpc::INTERNAL:
    case grpc::UNAVAILABLE:
      return true;

    default:
      return false;
  }
}

}  // namespace

std::unique_ptr<BigtableStore> BigtableStore::CreateFromFlagsOrDie() {
  if (FLAGS_for_testing_only_use_bigtable_emulator) {
    LOG(WARNING) << "*** Using an insecure connection to Bigtable Emulator "
                    "instead of using a secure connection to Cloud Bigtable. "
                    "***";
    return std::unique_ptr<BigtableStore>(
        BigtableStoreEmulatorFactory::NewStore());
  }

  // See https://developers.google.com/identity/protocols/ \
  //         application-default-credentials
  // for an explanation of grpc::GoogleDefaultCredentials(). When running
  // on GKE this should cause the service account to be used. When running
  // on a developer's machine this might either use the user's oauth credentials
  // or a service account if the user has installed one. To use a service
  // account the library looks for a key file located at the path specified in
  // the environment variable GOOGLE_APPLICATION_CREDENTIALS.
  CHECK_NE("", FLAGS_bigtable_project_name);
  CHECK_NE("", FLAGS_bigtable_instance_id);
  auto creds = grpc::GoogleDefaultCredentials();
  CHECK(creds);
  LOG(INFO) << "Connecting to CloudBigtable at " << kCloudBigtableUri << ", "
            << kCloudBigtableAdminUri;
  LOG(INFO) << "project=" << FLAGS_bigtable_project_name
            << " instance=" << FLAGS_bigtable_instance_id;
  return std::unique_ptr<BigtableStore>(new BigtableStore(
      kCloudBigtableUri, kCloudBigtableAdminUri, creds,
      FLAGS_bigtable_project_name, FLAGS_bigtable_instance_id));
}

BigtableStore::BigtableStore(
    const std::string& uri, const std::string& admin_uri,
    std::shared_ptr<grpc::ChannelCredentials> credentials,
    const std::string& project_name, const std::string& instance_id)
    : stub_(Bigtable::NewStub(grpc::CreateChannel(uri, credentials))),
      admin_stub_(BigtableTableAdmin::NewStub(
          grpc::CreateChannel(admin_uri, credentials))),
      observations_table_name_(
          BigtableNames::ObservationsTableName(project_name, instance_id)),
      report_progress_table_name_(
          BigtableNames::ReportMetadataTableName(project_name, instance_id)),
      report_rows_table_name_(
          BigtableNames::ReportRowsTableName(project_name, instance_id)) {}

std::string BigtableStore::TableName(DataStore::Table table) {
  switch (table) {
    case kObservations:
      return observations_table_name_;

    case kReportMetadata:
      return report_progress_table_name_;

    case kReportRows:
      return report_rows_table_name_;

    default:
      CHECK(false) << "unexpected table: " << table;
  }
}

Status BigtableStore::WriteRow(DataStore::Table table, DataStore::Row row) {
  std::vector<Row> rows;
  rows.emplace_back(std::move(row));
  return WriteRows(table, std::move(rows));
}

Status BigtableStore::WriteRows(DataStore::Table table,
                                std::vector<DataStore::Row> rows) {
  // We use the following simplistic strategy to perform retries with
  // exponential backoff.
  // (1) If any retryable error occurs we sleep and retry the entire operation.
  // (2) The sleep period starts with 10ms the first time and doubles each time
  //     until it reaches about 5 seconds.
  // (3) If we fail every time the sum of all sleep times is about 10 seconds.
  //
  // TODO(rudominer) The exponential backoff strategy can be made more
  // sophisticated in the following ways:
  // (a) We could randomize the sleep time up to an exponentially increasing
  //     maximum value.
  // (b) Instead of retrying the whole operation we could retry only the failed
  //     parts of the operation.
  // (c) Instead of always continuing the procedure up to about 10 seconds we
  //     could instead consider the pending RPC deadline from our own client.
  //     Currently the Shuffler does not set an RPC deadline.
  grpc::Status status;
  static const size_t kMaxAttempts = 11;
  int sleepmillis = 10;
  for (size_t attempt = 0; attempt < kMaxAttempts; attempt++) {
    status = DoWriteRows(table, rows);
    if (status.ok()) {
      return kOK;
    }
    if (!ShouldRetry(status)) {
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kWriteRowsFailure)
          << "Non-retryable error: " << ErrorMessage(status, "WriteRows");
      return GrpcStatusToStoreStatus(status);
    }
    if (attempt < kMaxAttempts - 1) {
      VLOG(1) << "Sleeping for " << sleepmillis << " ms.";
      std::this_thread::sleep_for(std::chrono::milliseconds(sleepmillis));
      sleepmillis *= 2;
    }
  }
  LOG_STACKDRIVER_COUNT_METRIC(ERROR, kWriteRowsFailure)
      << "Retried " << kMaxAttempts << " times without success. "
      << ErrorMessage(status, "WriteRows");
  return GrpcStatusToStoreStatus(status);
}

grpc::Status BigtableStore::DoWriteRows(
    DataStore::Table table, const std::vector<DataStore::Row>& rows) {
  MutateRowsRequest req;
  req.set_table_name(TableName(table));
  for (auto& row : rows) {
    auto entry = req.add_entries();
    entry->set_row_key(row.key);

    for (const auto& pair : row.column_values) {
      Mutation_SetCell* cell = entry->add_mutations()->mutable_set_cell();
      cell->set_family_name(kDataColumnFamilyName);
      // We Regex encode all values before using them as column names so that
      // we can use a regular expression to search for specific column names
      // later.
      std::string encoded_column_name;
      if (!RegexEncode(pair.first, &encoded_column_name)) {
        LOG_STACKDRIVER_COUNT_METRIC(ERROR, kDoWriteRowsFailure)
            << "RegexEncode failed on '" << pair.first << "'";
        return grpc::Status(grpc::INVALID_ARGUMENT, "RegexEncode failed.");
      }
      cell->mutable_column_qualifier()->swap(encoded_column_name);
      cell->set_value(pair.second);
    }
  }

  grpc::Status return_status = grpc::Status::OK;
  ClientContext context;
  std::unique_ptr<ClientReader<MutateRowsResponse>> reader(
      stub_->MutateRows(&context, req));

  MutateRowsResponse resp;
  while (reader->Read(&resp)) {
    for (const auto& entry : resp.entries()) {
      if (entry.status().code() != google::rpc::OK) {
        VLOG(1) << "MutateRows failed at entry " << entry.index()
                << " with error " << entry.status().message()
                << " code=" << entry.status().code();
        return_status = grpc::Status(grpc::StatusCode(entry.status().code()),
                                     entry.status().message());
      }
    }
  }

  grpc::Status status = reader->Finish();
  if (!status.ok()) {
    VLOG(1) << ErrorMessage(status, "MutateRows");
    return_status = status;
  }

  return return_status;
}

Status BigtableStore::ReadRow(Table table,
                              const std::vector<std::string>& column_names,
                              Row* row) {
  if (row == nullptr) {
    return kInvalidArguments;
  }
  auto read_response =
      ReadRowsInternal(table, row->key, true, row->key, true, column_names, 1);

  if (read_response.status != kOK) {
    return read_response.status;
  }

  if (read_response.rows.empty()) {
    return kNotFound;
  }

  DCHECK(read_response.rows.size() == 1) << read_response.rows.size();
  DCHECK(read_response.rows[0].key == row->key)
      << read_response.rows[0].key << " vs " << row->key;

  row->column_values.swap(read_response.rows[0].column_values);

  return kOK;
}

BigtableStore::ReadResponse BigtableStore::ReadRows(
    Table table, std::string start_row_key, bool inclusive,
    std::string limit_row_key, const std::vector<std::string>& column_names,
    size_t max_rows) {
  // Invoke ReadRowsInternal passing in false for |inclusive_end| indicating
  // that our interval is open on the right.
  return ReadRowsInternal(table, start_row_key, inclusive, limit_row_key, false,
                          column_names, max_rows);
}

BigtableStore::ReadResponse BigtableStore::ReadRowsInternal(
    Table table, std::string start_row_key, bool inclusive_start,
    std::string end_row_key, bool inclusive_end,
    const std::vector<std::string>& column_names, size_t max_rows) {
  ReadResponse read_response;
  read_response.status = kOK;
  if (max_rows == 0) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kReadRowsFailure) << "max_rows=0";
    read_response.status = kInvalidArguments;
    return read_response;
  }
  max_rows = std::min(max_rows, kMaxRowsReadLimit);

  ReadRowsRequest req;
  req.set_table_name(TableName(table));

  RowSet* rowset = req.mutable_rows();
  RowRange* row_range = rowset->add_row_ranges();

  if (inclusive_start) {
    row_range->mutable_start_key_closed()->swap(start_row_key);
  } else {
    row_range->mutable_start_key_open()->swap(start_row_key);
  }
  if (!end_row_key.empty()) {
    if (inclusive_end) {
      row_range->mutable_end_key_closed()->swap(end_row_key);
    } else {
      row_range->mutable_end_key_open()->swap(end_row_key);
    }
  }

  if (!column_names.empty()) {
    std::string column_filter;
    bool first = true;
    for (const auto& column_name : column_names) {
      if (!first) {
        column_filter += "|";
      }
      // Our column names are RegexEncoded.
      std::string encoded_column_name;
      if (!RegexEncode(column_name, &encoded_column_name)) {
        LOG_STACKDRIVER_COUNT_METRIC(ERROR, kReadRowsFailure)
            << "RegexEncode failed on '" << column_name << "'";
        read_response.status = kOperationFailed;
        return read_response;
      }
      column_filter += encoded_column_name;
      first = false;
    }
    req.mutable_filter()->mutable_column_qualifier_regex_filter()->swap(
        column_filter);
  }

  // We request one more row than we really want in order to be able
  // to set the |more_available| value in the response.
  req.set_rows_limit(max_rows + 1);

  ClientContext context;
  std::unique_ptr<ClientReader<ReadRowsResponse>> reader(
      stub_->ReadRows(&context, req));

  ReadRowsResponse resp;
  size_t num_complete_rows_read = 0;
  // The name of the current column for which we are receiving data. This
  // changes as the server sends us a chunk with a new "qualifier". (In
  // Bigtable lingo the "column qualifier" is what we are calling the column
  // name here.) The column names stored in Bigtable are RegexEncoded, but
  // we want to return the decoded version.
  std::string current_decoded_column_name = "";

  // We are using GRPC's Server Streaming feature to receive the response.
  // reader->Read() returns false to indicate that there will be no more
  // incoming messages, either because all the rows have been transmitted
  // or because the stream has failed or been canceled. (We detect these
  // latter cases by examining the Status returned from reader->Finish().)
  // It appears that it is necessary to keep reading until Read() returns
  // false, even if we have read as many rows as we want, because if we
  // leave the last row unread then the call to reader->Finish() below
  // will hang.
  while (reader->Read(&resp)) {
    for (const auto& chunk : resp.chunks()) {
      if (num_complete_rows_read == max_rows) {
        read_response.more_available = true;
        break;
      }

      // When we get a different row key, start a new row.
      if (read_response.rows.empty() ||
          (!chunk.row_key().empty() &&
           read_response.rows.back().key != chunk.row_key())) {
        read_response.rows.emplace_back();
        read_response.rows.back().key = chunk.row_key();
        // We are starting a new row so reset the current column.
        current_decoded_column_name = "";
      }
      auto& row = read_response.rows.back();
      if (!chunk.has_qualifier()) {
        // Keep using the same current column.
        CHECK(!current_decoded_column_name.empty());
      } else {
        // Update the current column.
        if (!RegexDecode(chunk.qualifier().value(),
                         &current_decoded_column_name)) {
          LOG_STACKDRIVER_COUNT_METRIC(ERROR, kReadRowsFailure)
              << "RegexEncode failed on '" << chunk.qualifier().value() << "'";
          read_response.status = kOperationFailed;
          return read_response;
        }
      }
      row.column_values[current_decoded_column_name] += chunk.value();
      if (chunk.commit_row()) {
        num_complete_rows_read++;
      }
      // TODO(rudominer) Handle chunk.reset_row(). For now we fail CHECK if
      // it happens just so we can keep track of if it is happening. But
      // ultimately this CHECK is not correct and we should handle this case
      // because I believe it can actually happen.
      CHECK(!chunk.reset_row());
    }
  }

  grpc::Status status = reader->Finish();

  if (!status.ok()) {
    // TODO(rudominer) Consider doing a retry here. Consider if this
    // method should be asynchronous.
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kReadRowsFailure)
        << ErrorMessage(status, "ReadRows");
    read_response.status = GrpcStatusToStoreStatus(status);
    return read_response;
  }

  read_response.status = kOK;
  return read_response;
}

Status BigtableStore::DeleteRow(Table table, std::string row_key) {
  MutateRowRequest req;
  req.set_table_name(TableName(table));
  req.mutable_row_key()->swap(row_key);
  req.add_mutations()->mutable_delete_from_row();

  ClientContext context;
  MutateRowResponse resp;
  grpc::Status status = stub_->MutateRow(&context, req, &resp);

  if (!status.ok()) {
    // TODO(rudominer) Consider doing a retry here. Consider if this
    // method should be asynchronous.
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kDeleteRowFailure)
        << ErrorMessage(status, "DeleteRow");
    return GrpcStatusToStoreStatus(status);
  }

  return kOK;
}

Status BigtableStore::DeleteRowsWithPrefix(Table table,
                                           std::string row_key_prefix) {
  DropRowRangeRequest req;
  req.set_name(TableName(table));
  req.mutable_row_key_prefix()->swap(row_key_prefix);

  ClientContext context;
  Empty resp;
  grpc::Status status = admin_stub_->DropRowRange(&context, req, &resp);

  if (!status.ok()) {
    // TODO(rudominer) Consider doing a retry here. Consider if this
    // method should be asynchronous.
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kDeleteRowsWithPrefixFailure)
        << ErrorMessage(status, "DeleteRowsWithPrefix");
    return GrpcStatusToStoreStatus(status);
  }

  return kOK;
}

Status BigtableStore::DeleteAllRows(Table table) {
  DropRowRangeRequest req;
  req.set_name(TableName(table));
  req.set_delete_all_data_from_table(true);

  ClientContext context;
  Empty resp;
  grpc::Status status = admin_stub_->DropRowRange(&context, req, &resp);

  if (!status.ok()) {
    // TODO(rudominer) Consider doing a retry here. Consider if this
    // method should be asynchronous.
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kDeleteAllRowsFailure)
        << ErrorMessage(status, "DeleteAllRows");
    return GrpcStatusToStoreStatus(status);
  }

  return kOK;
}

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

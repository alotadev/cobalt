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

#include "analyzer/store/data_store.h"

#include "analyzer/store/bigtable_store.h"
#include "analyzer/store/memory_store.h"
#include "gflags/gflags.h"
#include "glog/logging.h"

namespace cobalt {
namespace analyzer {
namespace store {

// Flags used to put the DataStore into testing/debug mode
DEFINE_bool(for_testing_only_use_memstore, false,
            "Use MemoryStore as the underlying data store");

DataStore::~DataStore() {}

std::shared_ptr<DataStore> DataStore::CreateFromFlagsOrDie() {
  if (FLAGS_for_testing_only_use_memstore) {
    LOG(WARNING)
        << "**** Using an in-memory data store instead of BigTable. ****";
    return std::shared_ptr<DataStore>(new MemoryStore());
  }

  return BigtableStore::CreateFromFlagsOrDie();
}

Status DataStore::DeleteRows(DataStore::Table table, std::string start_row_key,
                             bool inclusive, std::string limit_row_key) {
  std::string interval_start_row = std::move(start_row_key);
  ReadResponse read_response;
  do {
    std::vector<std::string> column_names;
    read_response = ReadRows(table, std::move(interval_start_row), inclusive,
                             limit_row_key, column_names, 1000);
    if (read_response.status != kOK) {
      return read_response.status;
    }
    if (read_response.rows.empty()) {
      DCHECK(!read_response.more_available);
      return kOK;
    }
    // Copy the last row key now to be the interval_start_row for next time
    // through the loop. Later we will move the string so we need to copy it
    // now.
    interval_start_row = read_response.rows.back().key;
    for (auto& row : read_response.rows) {
      Status status = DeleteRow(table, std::move(row.key));
      if (status != kOK) {
        return status;
      }
    }
  } while (read_response.more_available);
  return kOK;
}

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

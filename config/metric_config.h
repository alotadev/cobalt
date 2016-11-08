// Copyright 2016 The Fuchsia Authors
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

#ifndef COBALT_CONFIG_METRIC_CONFIG_H_
#define COBALT_CONFIG_METRIC_CONFIG_H_

#include "config/config.h"
#include "config/metrics.pb.h"

namespace cobalt {
namespace config {

// A container for all of the Metrics registered in Cobalt. This
// is used only on the Analyzer.
typedef Registry<RegisteredMetrics> MetricRegistry;

// A container for all of the ReportConfigs registered in Cobalt. This
// is used only on the Analyzer.
typedef Registry<RegisteredReports> ReportRegistry;

// For ease of understanding we specify the interfaces below as if
// MetricRegistry and  ReportRegistry were not template specializations but
// stand-alone classes.

/*
class MetricRegistry {
 public:
  // Populates a new instance of MetricRegistry by reading and parsing the
  // specified file. Returns a pair consisting of a pointer to the result and a
  // Status.
  //
  // If the operation is successful then the status is kOK. Otherwise the
  // Status indicates the error.
  //
  // If |error_collector| is not null then it will be notified of any parsing
  // errors or warnings.
  static std::pair<std::unique_ptr<MetricRegistry>, Status>
      FromFile(const std::string& file_path,
               google::protobuf::io::ErrorCollector* error_collector);

  // Returns the number of Metrics in this registry.
  size_t size();

  // Returns the MetricConfig with the given ID triple, or nullptr if there is
  // no such MetricConfig. The caller does not take ownership of the returned
  // pointer.
  const MetricConfig* const Get(uint32_t customer_id,
                                uint32_t project_id,
                                uint32_t id);
};

class ReportRegistry {
 public:
  // Populates a new instance of ReportRegistry by reading and parsing the
  // specified file. Returns a pair consisting of a pointer to the result and a
  // Status.
  //
  // If the operation is successful then the status is kOK. Otherwise the
  // Status indicates the error.
  //
  // If |error_collector| is not null then it will be notified of any parsing
  // errors or warnings.
  static std::pair<std::unique_ptr<ReportRegistry>, Status>
      FromFile(const std::string& file_path,
               google::protobuf::io::ErrorCollector* error_collector);

  // Returns the number of Reports in this registry.
  size_t size();

  // Returns the ReportConfig with the given ID triple, or nullptr if there is
  // no such ReportConfig. The caller does not take ownership of the returned
  // pointer.
  const ReportConfig* const Get(uint32_t customer_id,
                                uint32_t project_id,
                                uint32_t id);
};
*/

}  // namespace config
}  // namespace cobalt

#endif  // COBALT_CONFIG_METRIC_CONFIG_H_

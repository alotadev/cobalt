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

#ifndef COBALT_ANALYZER_REPORT_MASTER_REPORT_GENERATOR_H_
#define COBALT_ANALYZER_REPORT_MASTER_REPORT_GENERATOR_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "./encrypted_message.pb.h"
#include "./observation.pb.h"
#include "algorithms/forculus/forculus_analyzer.h"
#include "analyzer/store/observation_store.h"
#include "analyzer/store/report_store.h"
#include "config/analyzer_config.h"
#include "grpc++/grpc++.h"

namespace cobalt {
namespace analyzer {

// In Cobalt V0.1 ReportGenerator is a singleton, single-threaded object
// owned by the ReportMaster. In later versions of Cobalt, ReportGenerator
// will be a separate service.
//
// ReportGenerator is responsible for generating individual reports. It is not
// responsible for knowing anything about report schedules. It is not
// responsible for figuring out which interval of days a report should analyze.
// Those things are the responsibility of the ReportMaster.
//
// ReportGenerator knows how to generator single-variable reports,
// single-variable slices of two-variable reports, and joint two variable
// reports. A |report_id| specifies which of these types of reports it
// refers to.
//
// The ReportGenerator uses the ObservationStore and the ReportStore for
// its input and output. It reads ReportMetadata from the ReportStore,
// reads Observations from the ObservationStore, and writes ReportRows
// to the ReportStore.
class ReportGenerator {
 public:
  ReportGenerator(std::shared_ptr<config::AnalyzerConfig> analyzer_config,
                  std::shared_ptr<store::ObservationStore> observation_store,
                  std::shared_ptr<store::ReportStore> report_store);

  // Requests that the ReportGenerator generate the report with the given
  // |report_id|. This method is invoked by the ReportMaster after
  // the ReportMaster invokes ReportStore::StartNewReport(). The ReportGenerator
  // will query the ReportMetadata for the report with the given |report_id|
  // from the ReportStore. The ReportMetadata must be found and must indicate
  // that the report is in the IN_PROGRESS state which is the state it is in
  // immediately after ReportMaster invokes StartNewReport().
  //
  // The |first_day_index| and |last_day_index| from the ReportMetadata
  // determine the range of day indices over which analysis will be performed.
  // Since the ReportMaster is responsible for writing the ReportMetadata via
  // the call to StartNewReport, it is the ReportMaster and not the
  // ReportGenerator that determines the interval of days that should be
  // analyzed by the report.
  //
  // The |report_config_id| field of the |report_id| specifies the ID of
  // a ReportConfig that must be found in the |report_configs| registry that
  // was passed to the constructor. The report being generated is an instance
  // of this ReportConfig.
  //
  // The |variable_slice| field of the |report_id| specifies whether this
  // report is to analyze the first variable of the ReportConfig, to analyze
  // the second variable of the ReportConfig (if the ReportConfig has two
  // variables) or to perform a joint analysis on the two variables. In the
  // latter case the corresponding reports for the first and second variables
  // must already have been completed.
  //
  // The ReportGenerator will read the Observations to be analyzed from the
  // ObservationStre and will write the output of the analysis into the
  // ReportStore via the method ReportStore::AddReportRows().
  //
  // This method will return when the report generation is complete. It is then
  // the responsibility of the caller (i.e. the ReportMaster) to finish
  // the report by invoking ReportStore::EndReport().
  //
  // The returned status will be OK if the report was generated successfully
  // or an error status otherwise.
  grpc::Status GenerateReport(const ReportId& report_id);

 private:
  // This is a helper function for GenerateReport().
  //
  // Generates the single-variable report with the given |report_id|,
  // performing the analysis over the period [first_day_index, last_day_index].
  // |report_config| must be the associated ReportConfig,
  // |metric| must be the associated Metric and |single_part|
  // must be a vector of size 1 containing the name of the metric part being
  // analyzed. The |variable_slice| of |report_id| must be either
  // VARIABLE_1 or VARIABLE_2. This method does not know how to generate
  // JOINT reports.
  grpc::Status GenerateSingleVariableReport(const ReportId& report_id,
                                   const ReportConfig& report_config,
                                   const Metric& metric,
                                   std::vector<std::string> single_part,
                                   uint32_t start_day_index,
                                   uint32_t end_day_index);

  std::shared_ptr<config::AnalyzerConfig> analyzer_config_;
  std::shared_ptr<store::ObservationStore> observation_store_;
  std::shared_ptr<store::ReportStore> report_store_;
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_REPORT_MASTER_REPORT_GENERATOR_H_

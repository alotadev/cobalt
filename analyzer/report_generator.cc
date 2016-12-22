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

#include "analyzer/report_generator.h"

#include <glog/logging.h>

#include <map>
#include <memory>
#include <string>

// TODO(rudominer) Eliminate dependency on analyzer_service.h. Currently
// it is needed for ObservationKey.
#include "analyzer/analyzer_service.h"
#include "analyzer/schema.pb.h"

using cobalt::config::EncodingRegistry;
using cobalt::config::MetricRegistry;
using cobalt::config::ReportRegistry;
using cobalt::forculus::ForculusAnalyzer;
using cobalt::analyzer::schema::ObservationValue;

namespace cobalt {
namespace analyzer {

namespace {
// TODO(pseudorandom): implement
bool Decrypt(const std::string ciphertext, std::string* cleartext) {
  *cleartext = ciphertext;

  return true;
}
}  // namespace

ReportGenerator::ReportGenerator(std::shared_ptr<MetricRegistry> metrics,
                                 std::shared_ptr<ReportRegistry> reports,
                                 std::shared_ptr<EncodingRegistry> encodings,
                                 std::shared_ptr<Store> store)
    : metrics_(metrics),
      reports_(reports),
      encodings_(encodings),
      store_(store) {}

void ReportGenerator::GenerateReport(const ReportConfig& config) {
  LOG(INFO) << "Running report " << config.name();

  // Read the part of the DB pertinent to this report.
  ObservationKey keys[2];  // start, end keys.

  keys[1].set_max();

  for (ObservationKey& key : keys) {
    key.set_customer(config.customer_id());
    key.set_project(config.project_id());
    key.set_metric(config.metric_id());
  }

  std::map<std::string, std::string> db;
  int rc = store_->get_range(keys[0].MakeKey(), keys[1].MakeKey(), &db);

  if (rc != 0) {
    LOG(ERROR) << "get_range() error: " << rc;
    return;
  }

  LOG(INFO) << "Observations found: " << db.size();

  // As we process observations, we store results in analyzers_
  analyzers_.clear();

  // Try to decode all observations.
  for (const auto& i : db) {
    // Parse the database entry.
    ObservationValue entry;

    if (!entry.ParseFromString(i.second)) {
      LOG(ERROR) << "Can't parse ObservationValue.  Key: " << i.first;
      continue;
    }

    // Decrypt the observation.
    Observation obs;

    std::string cleartext;

    if (!Decrypt(entry.observation().ciphertext(), &cleartext)) {
      LOG(ERROR) << "Can't decrypt";
      continue;
    }

    if (!obs.ParseFromString(cleartext)) {
      LOG(ERROR) << "Can't parse Observation.  Key: " << i.first;
      continue;
    }

    // Process the observation.  This will populate analyzers_.
    ProcessObservation(config, entry.metadata(), obs);
  }

  // See what results are available.
  for (auto& analyzer : analyzers_) {
    ForculusAnalyzer& forculus = *analyzer.second;
    auto results = forculus.TakeResults();

    for (auto& result : results) {
      LOG(INFO) << "Found plain-text:" << result.first;
    }
  }
}

void ReportGenerator::ProcessObservation(const ReportConfig& config,
                                         const ObservationMetadata& metadata,
                                         const Observation& observation) {
  // Figure out which metric we're dealing with.
  const Metric* const metric = metrics_->Get(
      config.customer_id(), config.project_id(), metadata.metric_id());

  if (!metric) {
    LOG(ERROR) << "Can't find metric ID " << metadata.metric_id()
               << " for customer " << config.customer_id() << " project "
               << config.project_id();
    return;
  }

  // Process all the parts.
  for (const auto& i : observation.parts()) {
    const std::string& name = i.first;
    const ObservationPart& part = i.second;

    // Check that the part name is expected.
    if (metric->parts().find(name) == metric->parts().end()) {
      LOG(ERROR) << "Unknown part name: " << name;
      continue;
    }

    // Figure out how the part is encoded.
    uint32_t eid = part.encoding_config_id();
    const EncodingConfig* const enc =
        encodings_->Get(config.customer_id(), config.project_id(), eid);

    if (!enc) {
      LOG(ERROR) << "Unknown encoding: " << eid;
      continue;
    }

    // XXX only support forculus for now.
    if (!enc->has_forculus()) {
      LOG(ERROR) << "Unsupported encoding: " << eid;
      continue;
    }

    // Grab the decoder.
    ForculusAnalyzer* forculus;
    auto iter = analyzers_.find(eid);

    if (iter != analyzers_.end()) {
      forculus = iter->second.get();
    } else {
      ForculusConfig forculus_conf;

      forculus_conf.set_threshold(enc->forculus().threshold());

      forculus = new ForculusAnalyzer(forculus_conf);
      analyzers_[eid] = std::unique_ptr<ForculusAnalyzer>(forculus);
    }

    if (!forculus->AddObservation(metadata.day_index(), part.forculus())) {
      LOG(ERROR) << "Can't add observation";
      continue;
    }
  }

  if (observation.parts().size() != metric->parts().size()) {
    VLOG(1) << "Not all parts present in observation";
  }
}

}  // namespace analyzer
}  // namespace cobalt

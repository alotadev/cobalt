// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "encoder/project_context.h"

#include <memory>
#include <utility>

#include "./logging.h"
#include "config/encoding_config.h"
#include "config/metric_config.h"

namespace cobalt {
namespace encoder {

ProjectContext::ProjectContext(
    uint32_t customer_id, uint32_t project_id,
    std::shared_ptr<config::MetricRegistry> metric_registry,
    std::shared_ptr<config::EncodingRegistry> encoding_registry)
    : customer_id_(customer_id),
      project_id_(project_id),
      metric_registry_(metric_registry),
      encoding_registry_(encoding_registry) {}

ProjectContext::ProjectContext(
    uint32_t customer_id, uint32_t project_id,
    std::shared_ptr<config::ClientConfig> client_config)
    : customer_id_(customer_id),
      project_id_(project_id),
      client_config_(client_config) {}

const Metric* ProjectContext::Metric(uint32_t id) const {
  if (client_config_) {
    return client_config_->Metric(customer_id_, project_id_, id);
  }
  CHECK(metric_registry_);
  return metric_registry_->Get(customer_id_, project_id_, id);
}

const Metric* ProjectContext::Metric(const std::string& metric_name) const {
  if (client_config_) {
    return client_config_->Metric(customer_id_, project_id_, metric_name);
  }
  CHECK(metric_registry_);
  return metric_registry_->Get(customer_id_, project_id_, metric_name);
}

const std::unordered_map<std::string, uint32_t>&
ProjectContext::DefaultEncodingsForMetric(uint32_t id) {
  if (default_encodings_.find(id) == default_encodings_.end()) {
    std::unordered_map<std::string, uint32_t> encodings;
    const class Metric* metric;
    if (client_config_) {
      metric = client_config_->Metric(customer_id_, project_id_, id);
    } else {
      CHECK(metric_registry_);
      metric = metric_registry_->Get(customer_id_, project_id_, id);
    }

    if (metric) {
      for (auto pair : metric->parts()) {
        encodings.insert(
            std::make_pair(pair.first, pair.second.default_encoding_id()));
      }
    }
    default_encodings_.insert(std::make_pair(id, std::move(encodings)));
  }

  return default_encodings_[id];
}

const EncodingConfig* ProjectContext::EncodingConfig(uint32_t id) const {
  if (client_config_) {
    return client_config_->EncodingConfig(customer_id_, project_id_, id);
  }
  CHECK(encoding_registry_);
  return encoding_registry_->Get(customer_id_, project_id_, id);
}
}  // namespace encoder
}  // namespace cobalt

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger/project_context.h"

#include <sstream>

#include "./logging.h"

namespace cobalt {
namespace logger {

std::string MetricDebugString(const MetricDefinition& metric) {
  std::ostringstream stream;
  stream << metric.metric_name() << " (" << metric.id() << ")";
  return stream.str();
}

MetricRef::MetricRef(const Project* project,
                     const MetricDefinition* metric_definition)
    : project_(project), metric_definition_(metric_definition) {}

const Project& MetricRef::project() const { return *project_; }

const std::string MetricRef::ProjectDebugString() const {
#ifdef PROTO_LITE
  return project_->project_name();
#else
  return project_->DebugString();
#endif
}

uint32_t MetricRef::metric_id() const { return metric_definition_->id(); }

const std::string& MetricRef::metric_name() const {
  return metric_definition_->metric_name();
}

namespace {

void PopulateProject(uint32_t customer_id, uint32_t project_id,
                     const std::string& customer_name,
                     const std::string& project_name,
                     ReleaseStage release_stage, Project* project) {
  project->set_customer_id(customer_id);
  project->set_project_id(project_id);
  project->set_customer_name(std::move(customer_name));
  project->set_project_name(std::move(project_name));
  project->set_release_stage(release_stage);
}

}  // namespace

ProjectContext::ProjectContext(
    uint32_t customer_id, uint32_t project_id, std::string customer_name,
    std::string project_name,
    std::unique_ptr<MetricDefinitions> metric_definitions,
    ReleaseStage release_stage)
    : metric_definitions_(std::move(metric_definitions)) {
  CHECK(metric_definitions_);
  PopulateProject(customer_id, project_id, customer_name, project_name,
                  release_stage, &project_);
  for (const auto& metric : metric_definitions_->metric()) {
    if (metric.customer_id() == project_.customer_id() &&
        metric.project_id() == project_.project_id()) {
      metrics_by_name_[metric.metric_name()] = &metric;
      metrics_by_id_[metric.id()] = &metric;
    } else {
      LOG(ERROR) << "ProjectContext constructor found a MetricDefinition "
                    "for the wrong project. Expected customer "
                 << project_.customer_name()
                 << " (id=" << project_.customer_id() << "), project "
                 << project_.project_name() << " (id=" << project_.project_id()
                 << "). Found customer_id=" << metric.customer_id()
                 << " project_id=" << metric.project_id();
    }
  }
}

const MetricDefinition* ProjectContext::GetMetric(
    const uint32_t metric_id) const {
  auto iter = metrics_by_id_.find(metric_id);
  if (iter == metrics_by_id_.end()) {
    return nullptr;
  }
  return iter->second;
}

const MetricDefinition* ProjectContext::GetMetric(
    const std::string& metric_name) const {
  auto iter = metrics_by_name_.find(metric_name);
  if (iter == metrics_by_name_.end()) {
    return nullptr;
  }
  return iter->second;
}

const MetricRef ProjectContext::RefMetric(
    const MetricDefinition* metric_definition) const {
  return MetricRef(&project_, metric_definition);
}

const std::string ProjectContext::DebugString() const {
#ifdef PROTO_LITE
  return project_.project_name();
#else
  return project_.DebugString();
#endif
}

}  // namespace logger
}  // namespace cobalt

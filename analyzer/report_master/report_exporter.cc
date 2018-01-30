// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "analyzer/report_master/report_exporter.h"

#include <memory>
#include <sstream>
#include <thread>

#include "analyzer/report_master/report_serializer.h"
#include "analyzer/report_master/report_stream.h"
#include "glog/logging.h"
#include "util/log_based_metrics.h"

namespace cobalt {
namespace analyzer {

using util::gcs::GcsUtil;

// Stackdriver metric constants
namespace {
const char kExportReportFailure[] = "report-exporter-export-report-failure";
const char kUploadToGCSError[] = "gcs-uploader-upload-to-gcs-failure";
const char kPingBucketFailure[] = "gcs-uploader-ping-bucket-failure";
}  // namespace

namespace {

std::string ExtensionForMimeType(const std::string& mime_type) {
  if (mime_type == "text/csv") {
    return "csv";
  }
  return "";
}

}  // namespace

ReportExporter::ReportExporter(std::shared_ptr<GcsUploadInterface> uploader)
    : uploader_(uploader) {}

grpc::Status ReportExporter::ExportReport(const ReportConfig& report_config,
                                          const ReportMetadataLite& metadata,
                                          ReportRowIterator* row_iterator) {
  if (metadata.export_name().empty()) {
    // If we were not told to export this report, there is nothing to do.
    return grpc::Status::OK;
  }

  grpc::Status overall_status = grpc::Status::OK;
  bool first_export = true;
  for (const auto& export_config : report_config.export_configs()) {
    if (first_export) {
      first_export = false;
    } else {
      auto status = row_iterator->Reset();
      if (!status.ok()) {
        return status;
      }
    }
    auto status =
        ExportReportOnce(report_config, metadata, export_config, row_iterator);
    if (!status.ok()) {
      overall_status = status;
    }
  }
  return overall_status;
}

grpc::Status ReportExporter::ExportReportOnce(
    const ReportConfig& report_config, const ReportMetadataLite& metadata,
    const ReportExportConfig& export_config, ReportRowIterator* row_iterator) {
  ReportSerializer serializer(&report_config, &metadata, &export_config);
  ReportStream report_stream(&serializer, row_iterator);
  auto status = report_stream.Start();
  if (!status.ok()) {
    return status;
  }
  auto location_case = export_config.export_location_case();
  switch (location_case) {
    case ReportExportConfig::kGcs:
      return ExportReportToGCS(report_config, export_config.gcs(), metadata,
                               report_stream.mime_type(), &report_stream);
      break;

    default: {
      std::ostringstream stream;
      stream << "Unrecognized export_location: " << location_case;
      std::string message = stream.str();
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kExportReportFailure) << message;
      return grpc::Status(grpc::INTERNAL, message);
    }
  }
}

grpc::Status ReportExporter::ExportReportToGCS(
    const ReportConfig& report_config, const GCSExportLocation& location,
    const ReportMetadataLite& metadata, const std::string& mime_type,
    ReportStream* report_stream) {
  if (location.bucket().empty()) {
    std::string message = "CSVExportLocation has empty |bucket|";
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kExportReportFailure) << message;
    return grpc::Status(grpc::INVALID_ARGUMENT, message);
  }

  return uploader_->UploadToGCS(location.bucket(),
                                GcsPath(report_config, metadata, mime_type),
                                mime_type, report_stream);
}

std::string ReportExporter::GcsPath(const ReportConfig& report_config,
                                    const ReportMetadataLite& metadata,
                                    const std::string& mime_type) {
  std::ostringstream stream;
  stream << report_config.customer_id() << "_" << report_config.project_id()
         << "_" << report_config.id() << "/" << metadata.export_name();
  if (metadata.export_name().find('.') == std::string::npos) {
    std::string extension = ExtensionForMimeType(mime_type);
    if (!extension.empty()) {
      stream << "." << extension;
    }
  }
  return stream.str();
}

grpc::Status GcsUploader::UploadToGCS(const std::string& bucket,
                                      const std::string& path,
                                      const std::string& mime_type,
                                      ReportStream* report_stream) {
  if (!gcs_util_) {
    gcs_util_.reset(new GcsUtil());
    if (!gcs_util_->InitFromDefaultPaths()) {
      gcs_util_.reset();
      std::string message = "Unable to initialize GcsUtil.";
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kUploadToGCSError) << message;
      return grpc::Status(grpc::INTERNAL, message);
    }
    auto status = PingBucket(bucket);
    if (!status.ok()) {
      gcs_util_.reset();
      return status;
    }
  }
  int seconds_to_sleep = 1;
  for (int i = 0; i < 5; i++) {
    // We will allow up to 15 minutes to upload a single report to GCS.
    static const uint32_t kReportUploadTimeoutSeconds = 60 * 15;
    if (gcs_util_->Upload(bucket, path, mime_type, report_stream,
                          kReportUploadTimeoutSeconds) &&
        report_stream->status().ok()) {
      return grpc::Status::OK;
    }
    if (i < 4) {
      LOG(WARNING) << "Upload to GCS at " << bucket << "|" << path
                   << " failed. Sleeping for " << seconds_to_sleep
                   << " seconds before trying again.";
      std::this_thread::sleep_for(std::chrono::seconds(seconds_to_sleep));
      seconds_to_sleep *= 2;
    }
  }
  gcs_util_.reset();
  std::ostringstream stream;
  stream << "Upload to GCS at " << bucket << "|" << path
         << " failed five times. Giving up.";
  std::string message = stream.str();
  LOG_STACKDRIVER_COUNT_METRIC(ERROR, kUploadToGCSError) << message;
  return grpc::Status(grpc::INTERNAL, message);
}

grpc::Status GcsUploader::PingBucket(const std::string& bucket) {
  if (!gcs_util_) {
    gcs_util_.reset(new GcsUtil());
    if (!gcs_util_->InitFromDefaultPaths()) {
      gcs_util_.reset();
      std::string message = "Unable to initialize GcsUtil.";
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kPingBucketFailure) << message;
      return grpc::Status(grpc::INTERNAL, message);
    }
  }
  int seconds_to_sleep = 1;
  for (int i = 0; i < 5; i++) {
    if (gcs_util_->Ping(bucket)) {
      return grpc::Status::OK;
    }

    if (i < 4) {
      LOG(WARNING) << "Pinging " << bucket << " failed. Sleeping for "
                   << seconds_to_sleep << " seconds before trying again.";
      std::this_thread::sleep_for(std::chrono::seconds(seconds_to_sleep));
      seconds_to_sleep *= 2;
    }
  }
  gcs_util_.reset();
  std::ostringstream stream;
  stream << "Pinging " << bucket << " failed five times. Giving up.";
  std::string message = stream.str();
  LOG_STACKDRIVER_COUNT_METRIC(ERROR, kPingBucketFailure) << message;
  return grpc::Status(grpc::INTERNAL, message);
}

}  // namespace analyzer
}  // namespace cobalt

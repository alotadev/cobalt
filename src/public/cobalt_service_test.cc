// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/public/cobalt_service.h"

#include "src/lib/util/posix_file_system.h"
#include "third_party/abseil-cpp/absl/strings/escaping.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {

namespace {

CobaltConfig MinConfigForTesting() {
  CobaltConfig cfg = {.client_secret = system_data::ClientSecret::GenerateNewSecret()};

  cfg.file_system = std::make_unique<util::PosixFileSystem>();
  cfg.observation_store_directory = "/tmp/a";
  cfg.target_pipeline = std::make_unique<LocalPipeline>();

  return cfg;
}

}  // namespace

TEST(CobaltService, DoesNotCreateInternalLoggerWithNoGlobalRegistry) {
  auto cfg = MinConfigForTesting();
  cfg.global_registry = nullptr;
  CobaltService service(std::move(cfg));
  EXPECT_FALSE(service.has_internal_logger());
}

TEST(CobaltService, DoesNotCreateInternalLoggerWithEmptyGlobalRegistry) {
  auto cfg = MinConfigForTesting();
  cfg.global_registry = std::make_unique<CobaltRegistry>();
  CobaltService service(std::move(cfg));
  EXPECT_FALSE(service.has_internal_logger());
}

TEST(CobaltService, CreatesInternalLoggerWithValidRegistry) {
  auto cfg = MinConfigForTesting();
  cfg.global_registry = std::make_unique<CobaltRegistry>();
  std::string registry_bytes;
  ASSERT_TRUE(absl::Base64Unescape(cobalt::logger::kConfig, &registry_bytes));
  ASSERT_TRUE(cfg.global_registry->ParseFromString(registry_bytes));
  CobaltService service(std::move(cfg));
  EXPECT_TRUE(service.has_internal_logger());
}

}  // namespace cobalt

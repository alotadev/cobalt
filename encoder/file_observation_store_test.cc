// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <random>
#include <utility>

#include "./gtest.h"
#include "./logging.h"
#include "encoder/client_secret.h"
#include "encoder/encoder.h"
#include "encoder/fake_system_data.h"
#include "encoder/file_observation_store.h"
#include "util/posix_file_system.h"
// Generated from file_observation_store_test_config.yaml
#include "encoder/file_observation_store_test_config.h"

namespace cobalt {
namespace encoder {

using config::ClientConfig;
using util::EncryptedMessageMaker;
using util::PosixFileSystem;

namespace {

// These values must match the values specified in the invocation of
// generate_test_config_h() in CMakeLists.txt. and in the invocation of
// cobalt_config_header("generate_shipping_manager_test_config") in BUILD.gn.
const uint32_t kCustomerId = 1;
const uint32_t kProjectId = 1;

const size_t kNoOpEncodingByteOverhead = 34;
const size_t kMaxBytesPerObservation = 100;
const size_t kMaxBytesPerEnvelope = 400;
const size_t kMaxBytesTotal = 1000;

const std::string &test_dir_base = "/tmp/fos_test";

std::string GetTestDirName(const std::string &base) {
  std::stringstream fname;
  fname << base << "_"
        << std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
               .count();
  return fname.str();
}

// Returns a ProjectContext obtained by parsing the configuration specified
// in shipping_manager_test_config.yaml
std::shared_ptr<ProjectContext> GetTestProject() {
  // Parse the base64-encoded, serialized CobaltConfig in
  // shipping_manager_test_config.h. This is generated from
  // shipping_manager_test_config.yaml. Edit that yaml file to make changes. The
  // variable name below, |cobalt_config_base64|, must match what is
  // specified in the build files.
  std::unique_ptr<ClientConfig> client_config =
      ClientConfig::CreateFromCobaltConfigBase64(cobalt_config_base64);
  EXPECT_NE(nullptr, client_config);

  return std::shared_ptr<ProjectContext>(new ProjectContext(
      kCustomerId, kProjectId,
      std::shared_ptr<ClientConfig>(client_config.release())));
}

class FileObservationStoreTest : public ::testing::Test {
 public:
  FileObservationStoreTest()
      : encrypt_to_analyzer_("", EncryptedMessage::NONE),
        test_dir_name_(GetTestDirName(test_dir_base)),
        project_(GetTestProject()),
        encoder_(project_, ClientSecret::GenerateNewSecret(), &system_data_) {
    MakeStore();
  }

  void MakeStore() {
    store_.reset(new FileObservationStore(
        kMaxBytesPerObservation, kMaxBytesPerEnvelope, kMaxBytesTotal,
        std::make_unique<PosixFileSystem>(), test_dir_name_));
  }

  void TearDown() override { store_->Delete(); }

  ObservationStore::StoreStatus AddObservation(
      size_t num_bytes, uint32_t metric_id = kDefaultMetricId) {
    CHECK(num_bytes > kNoOpEncodingByteOverhead) << " num_bytes=" << num_bytes;
    Encoder::Result result = encoder_.EncodeString(
        metric_id, kNoOpEncodingId,
        std::string("x", num_bytes - kNoOpEncodingByteOverhead));
    auto message = std::make_unique<EncryptedMessage>();
    encrypt_to_analyzer_.Encrypt(*result.observation, message.get());
    return store_->AddEncryptedObservation(std::move(message),
                                           std::move(result.metadata));
  }

 private:
  EncryptedMessageMaker encrypt_to_analyzer_;

 protected:
  std::string test_dir_name_;
  std::unique_ptr<FileObservationStore> store_;
  FakeSystemData system_data_;
  std::shared_ptr<ProjectContext> project_;
  Encoder encoder_;
};

}  // namespace

TEST_F(FileObservationStoreTest, AddRetrieveSingleObservation) {
  EXPECT_EQ(ObservationStore::kOk, AddObservation(50));
  auto envelope = store_->TakeNextEnvelopeHolder();
  // Since we haven't written kMaxBytesPerEnvelope yet, there are no finalized
  // envelopes, TakeNextEnvelopeHolder should force the active file to finalize.
  EXPECT_NE(envelope, nullptr);
}

TEST_F(FileObservationStoreTest, AddRetrieveFullEnvelope) {
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(ObservationStore::kOk, AddObservation(100));
  }

  auto envelope = store_->TakeNextEnvelopeHolder();
  ASSERT_NE(envelope, nullptr);
  auto read_env = envelope->GetEnvelope();
  EXPECT_EQ(read_env.batch_size(), 1);
  EXPECT_EQ(read_env.batch(0).encrypted_observation_size(), 4);
}

TEST_F(FileObservationStoreTest, AddRetrieveMultipleFullEnvelopes) {
  for (int i = 0; i < 5 * 4; i++) {
    EXPECT_EQ(ObservationStore::kOk, AddObservation(100));
  }

  for (int i = 0; i < 5; i++) {
    auto envelope = store_->TakeNextEnvelopeHolder();
    ASSERT_NE(envelope, nullptr);
    auto read_env = envelope->GetEnvelope();
    EXPECT_EQ(read_env.batch_size(), 1);
    EXPECT_EQ(read_env.batch(0).encrypted_observation_size(), 4);
  }
}

TEST_F(FileObservationStoreTest, Add2FullAndReturn1) {
  for (int i = 0; i < 2 * 4; i++) {
    EXPECT_EQ(ObservationStore::kOk, AddObservation(100));
  }

  auto first_envelope = store_->TakeNextEnvelopeHolder();
  ASSERT_NE(first_envelope, nullptr);
  auto second_envelope = store_->TakeNextEnvelopeHolder();
  ASSERT_NE(second_envelope, nullptr);
  EXPECT_TRUE(store_->Empty());

  // Delete the second envelope
  second_envelope = nullptr;
  EXPECT_TRUE(store_->Empty());

  store_->ReturnEnvelopeHolder(std::move(first_envelope));
  EXPECT_FALSE(store_->Empty());
}

TEST_F(FileObservationStoreTest, RecoverAfterCrashWithNoObservations) {
  EXPECT_TRUE(store_->Empty());

  // Simulate the store crashing.
  store_ = nullptr;

  // Store restarts.
  MakeStore();

  // The store should still be empty.
  EXPECT_TRUE(store_->Empty());
}

TEST_F(FileObservationStoreTest, RecoverAfterCrash) {
  // Add some observations, but not enough to finalize.
  for (int i = 0; i < 3; i++) {
    EXPECT_EQ(ObservationStore::kOk, AddObservation(100));
    EXPECT_EQ(store_->ListFinalizedFiles().size(), 0u);
  }

  // Simulate the store crashing.
  store_ = nullptr;

  // Store restarts.
  MakeStore();

  // The store should finalize the in-progress envelope.
  EXPECT_FALSE(store_->Empty());
  EXPECT_EQ(store_->ListFinalizedFiles().size(), 1u);
}

TEST_F(FileObservationStoreTest, IgnoresUnexpectedFiles) {
  { std::ofstream dummy(test_dir_name_ + "/BAD_FILE"); }
  EXPECT_EQ(store_->ListFinalizedFiles().size(), 0u);
  EXPECT_EQ(store_->TakeNextEnvelopeHolder(), nullptr);

  { std::ofstream empty_invalid(test_dir_name_ + "/10000000-100000.data"); }
  EXPECT_EQ(store_->ListFinalizedFiles().size(), 0u);
  EXPECT_EQ(store_->TakeNextEnvelopeHolder(), nullptr);

  { std::ofstream empty_valid(test_dir_name_ + "/1234567890123-1234567.data"); }
  EXPECT_EQ(store_->ListFinalizedFiles().size(), 1u);
  EXPECT_NE(store_->TakeNextEnvelopeHolder(), nullptr);
}

TEST_F(FileObservationStoreTest, HandlesCorruptFiles) {
  {
    std::ofstream file(test_dir_name_ + "/1234567890123-1234567.data");
    file << "CORRUPT DATA!!!";
  }
  EXPECT_EQ(store_->ListFinalizedFiles().size(), 1u);
  auto env = store_->TakeNextEnvelopeHolder();
  ASSERT_NE(env, nullptr);

  auto read_env = env->GetEnvelope();
  EXPECT_EQ(read_env.batch_size(), 0);
}

TEST_F(FileObservationStoreTest, StressTest) {
  std::random_device rd;
  for (int i = 0; i < 5000; i++) {
    // Between 5-15 observations.
    auto observations = (rd() % 10) + 5;
    // Between 50-100 bytes per observation.
    auto size = (rd() % 50) + 50;
    for (auto j = 0u; j < observations; j++) {
      EXPECT_EQ(ObservationStore::kOk, AddObservation(size));
    }

    while (true) {
      auto holder = store_->TakeNextEnvelopeHolder();
      if (holder == nullptr) {
        break;
      }

      auto should_return = rd() % 2;
      if (should_return == 1) {
        store_->ReturnEnvelopeHolder(std::move(holder));
      } else {
        auto env = holder->GetEnvelope();
        ASSERT_GT(env.batch_size(), 0);
      }
    }

    ASSERT_EQ(store_->Size(), 0u);
  }
}

}  // namespace encoder
}  // namespace cobalt

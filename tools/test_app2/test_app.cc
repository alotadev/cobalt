// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/test_app2/test_app.h"

#include <libgen.h>

#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "config/cobalt_config.pb.h"
#include "config/metric_definition.pb.h"
#include "config/project_configs.h"
#include "encoder/memory_observation_store.h"
#include "encoder/shipping_manager.h"
#include "encoder/system_data.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "logger/encoder.h"
#include "logger/project_context.h"
#include "util/clearcut/curl_http_client.h"
#include "util/pem_util.h"

namespace cobalt {

using config::ProjectConfigs;
using encoder::ClearcutV1ShippingManager;
using encoder::ClientSecret;
using encoder::MemoryObservationStore;
using encoder::ShippingManager;
using encoder::SystemData;
using encoder::SystemDataInterface;
using logger::Encoder;
using logger::Logger;
using logger::LoggerInterface;
using logger::ProjectContext;
using util::EncryptedMessageMaker;
using util::PemUtil;

// There are three modes of operation of the Cobalt TestClient program
// determined by the value of this flag.
// - interactive: The program runs an interactive command-loop.
// - send-once: The program sends a single Envelope described by flags.
// - automatic: The program runs forever sending many Envelopes with randomly
//              generated values.
DEFINE_string(mode, "interactive",
              "This program may be used in 3 modes: 'interactive', "
              "'send-once', 'automatic'");

DEFINE_string(customer_name, "fuchsia", "Customer name");
DEFINE_string(project_name, "test_app2", "Project name");
DEFINE_string(metric_name, "error_occurred", "Initial Metric name");

DEFINE_string(analyzer_pk_pem_file, "",
              "Path to a file containing a PEM encoding of the public key of "
              "the Analyzer used for Cobalt's internal encryption scheme. If "
              "not specified then no encryption will be used.");
DEFINE_string(shuffler_pk_pem_file, "",
              "Path to a file containing a PEM encoding of the public key of "
              "the Shuffler used for Cobalt's internal encryption scheme. If "
              "not specified then no encryption will be used.");

DEFINE_string(config_bin_proto_path, "",
              "Path to the serialized CobaltConfig proto from which the "
              "configuration is to be read. (Optional)");

DEFINE_string(clearcut_endpoint, "https://jmt17.google.com/log",
              "The URL to send clearcut requests to.");

namespace {

const size_t kMaxBytesPerObservation = 100 * 1024;
const size_t kMaxBytesPerEnvelope = 1024 * 1024;
const size_t kMaxBytesTotal = 10 * 1024 * 1024;
const std::chrono::seconds kDeadlinePerSendAttempt(60);

// Prints help for the interactive mode.
void PrintHelp(std::ostream* ostream) {
  *ostream << "Cobalt command-line testing client" << std::endl;
  *ostream << "----------------------------------" << std::endl;
  *ostream << "help                     \tPrint this help message."
           << std::endl;
  *ostream << "log <num> event <index>  \tLog <num> independent copies "
              "of the event with event_type_index = <index>"
           << std::endl;
  *ostream << "ls                       \tList current values of "
              "parameters."
           << std::endl;
  *ostream << "send                     \tSend all previously encoded "
              "observations and clear the observation cache."
           << std::endl;
  *ostream << "set metric <name>        \tSet metric." << std::endl;
  *ostream
      << "show config              \tDisplay the current Metric definition."
      << std::endl;
  *ostream << "quit                     \tQuit." << std::endl;
  *ostream << std::endl;
}

// Returns the path to the standard Cobalt configuration based on the presumed
// location of this binary.
std::string FindCobaltConfigProto(char* argv[]) {
  char path[PATH_MAX], path2[PATH_MAX];

  // Get the directory of this binary.
  if (!realpath(argv[0], path)) {
    LOG(FATAL) << "realpath(): " << argv[0];
  }
  char* dir = dirname(path);
  // Set the relative path to the registry.
  snprintf(path2, sizeof(path2),
           "%s/../../third_party/config/cobalt_config.binproto", dir);

  // Get the absolute path to the registry.
  if (!realpath(path2, path)) {
    LOG(FATAL) << "Computed path to serialized CobaltConfig is invalid: "
               << path;
  }

  return path;
}

// Parses the mode flag.
TestApp::Mode ParseMode() {
  if (FLAGS_mode == "interactive") {
    return TestApp::kInteractive;
  }
  if (FLAGS_mode == "send-once") {
    return TestApp::kSendOnce;
  }
  if (FLAGS_mode == "automatic") {
    return TestApp::kAutomatic;
  }
  LOG(FATAL) << "Unrecognized mode: " << FLAGS_mode;
}

// Reads the PEM file at the specified path and writes the contents into
// |*pem_out|. Returns true for success or false for failure.
bool ReadPublicKeyPem(const std::string& pem_file, std::string* pem_out) {
  VLOG(2) << "Reading PEM file at " << pem_file;
  if (PemUtil::ReadTextFile(pem_file, pem_out)) {
    return true;
  }
  LOG(ERROR) << "Unable to open PEM file at " << pem_file
             << ". Skipping encryption!";
  return false;
}

// Reads the specified serialized CobaltConfig proto. Returns a ProjectContext
// containing the read config and the values of the -customer and
// -project flags.
std::unique_ptr<ProjectContext> LoadProjectContext(
    const std::string& config_bin_proto_path) {
  VLOG(2) << "Loading Cobalt configuration from " << config_bin_proto_path;

  std::ifstream config_file_stream;
  config_file_stream.open(config_bin_proto_path);
  CHECK(config_file_stream)
      << "Could not open cobalt config proto file: " << config_bin_proto_path;

  // Parse the cobalt config file.
  auto cobalt_config = std::make_unique<cobalt::CobaltConfig>();
  CHECK(cobalt_config->ParseFromIstream(&config_file_stream))
      << "Could not parse the cobalt config proto file: "
      << config_bin_proto_path;
  auto project_configs =
      std::make_unique<ProjectConfigs>(std::move(cobalt_config));

  // Retrieve the customer specified by the flags.
  const auto* customer_config =
      project_configs->GetCustomerConfig(FLAGS_customer_name);
  CHECK(customer_config) << "No such customer: " << FLAGS_customer_name << ".";

  // Retrieve the project specified by the flags.
  const auto* project_config = project_configs->GetProjectConfig(
      FLAGS_customer_name, FLAGS_project_name);
  CHECK(project_config) << "No such project: " << FLAGS_customer_name << "."
                        << FLAGS_project_name << ".";

  // Copy the MetricDefinitions
  auto metric_definitions = std::make_unique<MetricDefinitions>();
  metric_definitions->mutable_metric()->CopyFrom(project_config->metrics());
  return std::make_unique<ProjectContext>(
      customer_config->customer_id(), project_config->project_id(),
      FLAGS_customer_name, FLAGS_project_name, std::move(metric_definitions));
}

// Given a |line| of text, breaks it into tokens separated by white space.
std::vector<std::string> Tokenize(const std::string& line) {
  std::istringstream line_stream(line);
  std::vector<std::string> tokens;
  do {
    std::string token;
    line_stream >> token;
    std::remove(token.begin(), token.end(), ' ');
    if (!token.empty()) {
      tokens.push_back(token);
    }
  } while (line_stream);
  return tokens;
}

class RealLoggerFactory : public LoggerFactory {
 public:
  virtual ~RealLoggerFactory() = default;

  RealLoggerFactory(
      std::unique_ptr<EncryptedMessageMaker> observation_encrypter,
      std::unique_ptr<EncryptedMessageMaker> envelope_encrypter,
      std::unique_ptr<ProjectContext> project_context,
      std::unique_ptr<MemoryObservationStore> observation_store,
      std::unique_ptr<ClearcutV1ShippingManager> shipping_manager,
      std::unique_ptr<SystemDataInterface> system_data);

  std::unique_ptr<LoggerInterface> NewLogger() override;
  bool SendAccumulatedObservtions() override;
  const ProjectContext* project_context() override {
    return project_context_.get();
  }

 private:
  std::unique_ptr<EncryptedMessageMaker> observation_encrypter_;
  std::unique_ptr<EncryptedMessageMaker> envelope_encrypter_;
  std::unique_ptr<ProjectContext> project_context_;
  std::unique_ptr<MemoryObservationStore> observation_store_;
  std::unique_ptr<ClearcutV1ShippingManager> shipping_manager_;
  std::unique_ptr<SystemDataInterface> system_data_;
  std::unique_ptr<Encoder> encoder_;
};

RealLoggerFactory::RealLoggerFactory(
    std::unique_ptr<EncryptedMessageMaker> observation_encrypter,
    std::unique_ptr<EncryptedMessageMaker> envelope_encrypter,
    std::unique_ptr<ProjectContext> project_context,
    std::unique_ptr<MemoryObservationStore> observation_store,
    std::unique_ptr<ClearcutV1ShippingManager> shipping_manager,
    std::unique_ptr<SystemDataInterface> system_data)
    : observation_encrypter_(std::move(observation_encrypter)),
      envelope_encrypter_(std::move(envelope_encrypter)),
      project_context_(std::move(project_context)),
      observation_store_(std::move(observation_store)),
      shipping_manager_(std::move(shipping_manager)),
      system_data_(std::move(system_data)) {}

std::unique_ptr<LoggerInterface> RealLoggerFactory::NewLogger() {
  encoder_.reset(
      new Encoder(ClientSecret::GenerateNewSecret(), system_data_.get()));
  return std::unique_ptr<LoggerInterface>(new Logger(
      encoder_.get(), observation_store_.get(), shipping_manager_.get(),
      observation_encrypter_.get(), project_context_.get()));
}

bool RealLoggerFactory::SendAccumulatedObservtions() {
  shipping_manager_->RequestSendSoon();
  shipping_manager_->WaitUntilIdle(kDeadlinePerSendAttempt);
  auto status = shipping_manager_->last_send_status();
  return status.ok();
}

}  // namespace

std::unique_ptr<TestApp> TestApp::CreateFromFlagsOrDie(int argc, char* argv[]) {
  std::string config_bin_proto_path = FLAGS_config_bin_proto_path;
  // If no path is given, try to deduce it from the binary location.
  if (config_bin_proto_path == "") {
    config_bin_proto_path = FindCobaltConfigProto(argv);
  }

  std::unique_ptr<ProjectContext> project_context =
      LoadProjectContext(config_bin_proto_path);

  auto mode = ParseMode();

  auto analyzer_encryption_scheme = EncryptedMessage::NONE;
  std::string analyzer_public_key_pem = "";
  if (FLAGS_analyzer_pk_pem_file.empty()) {
    VLOG(2) << "WARNING: Encryption of Observations to the Analzyer not being "
               "used. Pass the flag -analyzer_pk_pem_file";
  } else if (ReadPublicKeyPem(FLAGS_analyzer_pk_pem_file,
                              &analyzer_public_key_pem)) {
    analyzer_encryption_scheme = EncryptedMessage::HYBRID_ECDH_V1;
  }
  auto shuffler_encryption_scheme = EncryptedMessage::NONE;
  std::string shuffler_public_key_pem = "";
  if (FLAGS_shuffler_pk_pem_file.empty()) {
    VLOG(2) << "WARNING: Encryption of Envelopes to the Shuffler not being "
               "used. Pass the flag -shuffler_pk_pem_file";
  } else if (ReadPublicKeyPem(FLAGS_shuffler_pk_pem_file,
                              &shuffler_public_key_pem)) {
    shuffler_encryption_scheme = EncryptedMessage::HYBRID_ECDH_V1;
  }
  std::unique_ptr<SystemDataInterface> system_data(new SystemData("test_app"));

  auto observation_encrypter = std::make_unique<EncryptedMessageMaker>(
      analyzer_public_key_pem, analyzer_encryption_scheme);
  auto envelope_encrypter = std::make_unique<EncryptedMessageMaker>(
      shuffler_public_key_pem, shuffler_encryption_scheme);
  auto observation_store = std::make_unique<MemoryObservationStore>(
      kMaxBytesPerObservation, kMaxBytesPerEnvelope, kMaxBytesTotal);

  // By using (kMaxSeconds, 0) here we are effectively putting the
  // ShippingDispatcher in manual mode. It will never send
  // automatically and it will send immediately in response to
  // RequestSendSoon().
  auto schedule_params = ShippingManager::ScheduleParams(
      ShippingManager::kMaxSeconds, std::chrono::seconds(0));
  if (mode == TestApp::kAutomatic) {
    // In automatic mode, let the ShippingManager send to the Shuffler
    // every 10 seconds.
    schedule_params = ShippingManager::ScheduleParams(std::chrono::seconds(10),
                                                      std::chrono::seconds(1));
  }
  auto shipping_manager = std::make_unique<ClearcutV1ShippingManager>(
      schedule_params, observation_store.get(), envelope_encrypter.get(),
      std::make_unique<clearcut::ClearcutUploader>(
          FLAGS_clearcut_endpoint,
          std::make_unique<util::clearcut::CurlHTTPClient>()));
  shipping_manager->Start();

  std::unique_ptr<LoggerFactory> logger_factory(new RealLoggerFactory(
      std::move(observation_encrypter), std::move(envelope_encrypter),
      std::move(project_context), std::move(observation_store),
      std::move(shipping_manager), std::move(system_data)));

  std::unique_ptr<TestApp> test_app(new TestApp(
      std::move(logger_factory), FLAGS_metric_name, mode, &std::cout));
  return test_app;
}

TestApp::TestApp(std::unique_ptr<LoggerFactory> logger_factory,
                 const std::string initial_metric_name, Mode mode,
                 std::ostream* ostream)
    : mode_(mode),
      logger_factory_(std::move(logger_factory)),
      ostream_(ostream) {
  CHECK(logger_factory_);
  CHECK(logger_factory_->project_context());
  CHECK(ostream_);
  CHECK(SetMetric(initial_metric_name));
}

bool TestApp::SetMetric(const std::string& metric_name) {
  auto metric = logger_factory_->project_context()->GetMetric(metric_name);
  if (!metric) {
    (*ostream_) << "There is no metric named '" << metric_name
                << "' in  project "
                << logger_factory_->project_context()->DebugString() << "."
                << std::endl;
    return false;
  }
  current_metric_ = metric;
  return true;
}

void TestApp::Run() {
  switch (mode_) {
    case kInteractive:
      CommandLoop();
      break;
    default:
      CHECK(false) << "Only interactive mode is coded so far.";
  }
}

void TestApp::CommandLoop() {
  std::string command_line;
  while (true) {
    *ostream_ << "Command or 'help': ";
    getline(std::cin, command_line);
    if (!ProcessCommandLine(command_line)) {
      break;
    }
  }
}

bool TestApp::ProcessCommandLine(const std::string command_line) {
  return ProcessCommand(Tokenize(command_line));
}

bool TestApp::ProcessCommand(const std::vector<std::string>& command) {
  if (command.empty()) {
    return true;
  }

  if (command[0] == "help") {
    PrintHelp(ostream_);
    return true;
  }

  if (command[0] == "log") {
    Log(command);
    return true;
  }

  if (command[0] == "ls") {
    ListParameters();
    return true;
  }

  if (command[0] == "send") {
    Send(command);
    return true;
  }

  if (command[0] == "set") {
    SetParameter(command);
    return true;
  }

  if (command[0] == "show") {
    Show(command);
    return true;
  }

  if (command[0] == "quit") {
    return false;
  }

  *ostream_ << "Unrecognized command: " << command[0] << std::endl;

  return true;
}

// We know that command[0] = "log"
void TestApp::Log(const std::vector<std::string>& command) {
  if (command.size() < 2) {
    *ostream_ << "Malformed log command. Expected <num> argument after 'log'."
              << std::endl;
    return;
  }

  int64_t num_clients;
  if (!ParseNonNegativeInt(command[1], true, &num_clients)) {
    return;
  }
  if (num_clients <= 0) {
    *ostream_ << "Malformed log command. <num> must be positive: "
              << num_clients << std::endl;
    return;
  }

  if (command.size() < 3) {
    *ostream_ << "Malformed log command. Expected log method to be specified "
                 "after <num>."
              << std::endl;
    return;
  }

  if (command[2] == "event") {
    LogEvent(num_clients, command);
    return;
  }

  *ostream_ << "Unrecognized log method specified: " << command[2] << std::endl;
  return;
}

// We know that command[0] = "log", command[1] = <num_clients>
void TestApp::LogEvent(uint64_t num_clients,
                       const std::vector<std::string>& command) {
  if (command.size() != 4) {
    *ostream_ << "Malformed log event command. Expected exactly one more "
                 "argument for <event_type_index>."
              << std::endl;
    return;
  }

  int64_t event_type_index;
  if (!ParseNonNegativeInt(command[3], true, &event_type_index)) {
    return;
  }

  LogEvent(num_clients, event_type_index);
}

void TestApp::LogEvent(size_t num_clients, uint32_t event_type_index) {
  if (!current_metric_) {
    *ostream_ << "Cannot LogEvent. There is no current metric set."
              << std::endl;
    return;
  }
  VLOG(6) << "TestApp::LogEvents(" << num_clients << ", " << event_type_index
          << ").";
  for (size_t i = 0; i < num_clients; i++) {
    auto logger = logger_factory_->NewLogger();
    auto status = logger->LogEvent(current_metric_->id(), event_type_index);
    if (status != logger::kOK) {
      LOG(ERROR) << "LogEvent() failed with status " << status
                 << ". metric=" << current_metric_->metric_name()
                 << ". event_type_index=" << event_type_index;
      break;
    }
  }
}

void TestApp::ListParameters() {
  std::string metric_name = "No metric set";
  if (current_metric_) {
    metric_name = current_metric_->metric_name();
  }
  *ostream_ << std::endl;
  *ostream_ << "Settable values" << std::endl;
  *ostream_ << "---------------" << std::endl;
  *ostream_ << "Metric: '" << metric_name << "'" << std::endl;
  *ostream_ << std::endl;
  *ostream_ << "Values set by flag at startup." << std::endl;
  *ostream_ << "-----------------------------" << std::endl;
  *ostream_ << "Customer: "
            << logger_factory_->project_context()->project().customer_name()
            << std::endl;
  *ostream_ << "Project: "
            << logger_factory_->project_context()->project().project_name()
            << std::endl;
  *ostream_ << "Clearcut endpoint: " << FLAGS_clearcut_endpoint << std::endl;
  *ostream_ << std::endl;
}

void TestApp::SetParameter(const std::vector<std::string>& command) {
  if (command.size() != 3) {
    *ostream_ << "Malformed set command. Expected 2 additional arguments."
              << std::endl;
    return;
  }

  if (command[1] == "metric") {
    if (SetMetric(command[2])) {
      *ostream_ << "Metric set." << std::endl;
    } else {
      *ostream_ << "Current metric unchanged." << std::endl;
    }
  } else {
    *ostream_ << command[1] << " is not a settable parameter." << std::endl;
  }
}

void TestApp::Send(const std::vector<std::string>& command) {
  if (command.size() != 1) {
    *ostream_ << "The send command doesn't take any arguments." << std::endl;
    return;
  }

  if (logger_factory_->SendAccumulatedObservtions()) {
    if (mode_ == TestApp::kInteractive) {
      std::cout << "Send to server succeeded." << std::endl;
    } else {
      VLOG(2) << "Send to server succeeded";
    }
  } else {
    if (mode_ == TestApp::kInteractive) {
      std::cout << "Send to server failed." << std::endl;
    } else {
      LOG(ERROR) << "Send to server failed.";
    }
  }
}

void TestApp::Show(const std::vector<std::string>& command) {
  // show config is currently the only show command.
  if (command.size() != 2 || command[1] != "config") {
    *ostream_ << "Expected 'show config'." << std::endl;
    return;
  }

  if (!current_metric_) {
    *ostream_ << "There is no current metric set." << std::endl;
  } else {
    *ostream_ << "Metric '" << current_metric_->metric_name() << "'"
              << std::endl;
    *ostream_ << "-----------------" << std::endl;
    *ostream_ << current_metric_->DebugString();
    *ostream_ << std::endl;
  }
}

bool TestApp::ParseNonNegativeInt(const std::string& str, bool complain,
                                  int64_t* x) {
  CHECK(x);
  std::istringstream iss(str);
  *x = -1;
  iss >> *x;
  char c;
  if (*x == -1 || iss.fail() || iss.get(c)) {
    if (complain) {
      if (mode_ == kInteractive) {
        *ostream_ << "Expected non-negative integer instead of " << str << "."
                  << std::endl;
      } else {
        LOG(ERROR) << "Expected non-negativea integer instead of " << str;
      }
    }
    return false;
  }
  return true;
}

}  // namespace cobalt

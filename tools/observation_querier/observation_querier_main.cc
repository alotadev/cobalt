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

#import "tools/observation_querier/observation_querier.h"

#include "gflags/gflags.h"
#include "glog/logging.h"

int main(int argc, char* argv[]) {
  google::SetUsageMessage(
      "Cobalt ObservationStore query tool.\n"
      "There are two modes of operation controlled by the -interactive flag:\n"
      "interactive: The program runs an interactive command-loop that "
      "may be used to query the ObservationStore.\n"
      "non-interactive: The program performs a single query and \n"
      "writes the results to std out.");
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  auto app = cobalt::ObservationQuerier::CreateFromFlagsOrDie();
  app->Run();

  exit(0);
}

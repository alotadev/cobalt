// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_UTIL_CLEARCUT_CURL_HTTP_CLIENT_H_
#define COBALT_UTIL_CLEARCUT_CURL_HTTP_CLIENT_H_

#include "third_party/clearcut/http_client.h"
#include "third_party/statusor/statusor.h"

namespace cobalt {
namespace util {
namespace clearcut {

using ::clearcut::HTTPClient;
using ::clearcut::HTTPRequest;
using ::clearcut::HTTPResponse;
using statusor::StatusOr;

// CurlHTTPClient implements clearcut::HTTPClient with a curl backend. This is
// a basic implementation that is designed to be used on linux clients (not
// fuchsia).
class CurlHTTPClient : public clearcut::HTTPClient {
 public:
  CurlHTTPClient();

  std::future<StatusOr<clearcut::HTTPResponse>> Post(
      clearcut::HTTPRequest request,
      std::chrono::steady_clock::time_point deadline);

  static bool global_init_called_;
};

}  // namespace clearcut
}  // namespace util
}  // namespace cobalt

#endif  // COBALT_UTIL_CLEARCUT_CURL_HTTP_CLIENT_H_

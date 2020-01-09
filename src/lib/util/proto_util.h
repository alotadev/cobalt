// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LIB_UTIL_PROTO_UTIL_H_
#define COBALT_SRC_LIB_UTIL_PROTO_UTIL_H_

#include <string>

#include <google/protobuf/message_lite.h>

#include "src/logging.h"

namespace cobalt {
namespace util {

// Given a proto message, populates a string with the base64 encoding of the
// serialized message and returns |true| on success. Logs an error and returns
// |false| if either serialization or base64-encoding fails.
bool SerializeToBase64(const ::google::protobuf::MessageLite& message,
                       std::string* encoded_message);

}  // namespace util
}  // namespace cobalt

#endif  // COBALT_SRC_LIB_UTIL_PROTO_UTIL_H_

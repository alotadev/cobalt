// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/logger/observation_writer.h"

#include <memory>
#include <utility>

#include "src/logging.h"
#include "src/observation_store/observation_store.h"
#include "src/pb/observation2.pb.h"
#include "src/tracing.h"

namespace cobalt::logger {

using ::cobalt::observation_store::ObservationStoreWriterInterface;
using observation_store::ObservationStore;

Status ObservationWriter::WriteObservation(const Observation2 &observation,
                                           std::unique_ptr<ObservationMetadata> metadata) const {
  TRACE_DURATION("cobalt_core", "ObservationWriter::WriteObservation");
  ObservationStore::StoreStatus store_status;
  if (observation_encrypter_) {
    auto encrypted_observation = std::make_unique<EncryptedMessage>();
    if (!observation_encrypter_->Encrypt(observation, encrypted_observation.get())) {
      LOG(ERROR) << "Encryption of an Observation failed.";
      return kOther;
    }
    store_status =
        observation_store_->StoreObservation(std::move(encrypted_observation), std::move(metadata));
  } else {
    // Make a copy of observation to add to the store.
    store_status = observation_store_->StoreObservation(std::make_unique<Observation2>(observation),
                                                        std::move(metadata));
  }
  if (store_status != ObservationStoreWriterInterface::kOk) {
    LOG_FIRST_N(ERROR, 10) << "ObservationStore::StoreObservation() failed with status "
                           << store_status;
    return kOther;
  }
  update_recipient_->NotifyObservationsAdded();
  return kOK;
}

}  // namespace cobalt::logger

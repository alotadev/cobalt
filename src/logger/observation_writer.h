// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LOGGER_OBSERVATION_WRITER_H_
#define COBALT_SRC_LOGGER_OBSERVATION_WRITER_H_

#include <memory>

#include "src/lib/util/encrypted_message_util.h"
#include "src/logger/status.h"
#include "src/observation_store/observation_store.h"
#include "src/observation_store/observation_store_update_recipient.h"
#include "src/pb/observation2.pb.h"

namespace cobalt {
namespace logger {

// The ObservationWriter encrypts Observations and writes them to the
// ObservationStore.
//
// A system has a single instance of ObservationWriter, which is used by the
// EventAggregator and multiple Loggers.
class ObservationWriter {
 public:
  // Constructor:
  //
  // |observation_store| A writer interface to the system's singleton instance of Observation Store.
  // This must remain valid as long as the ObservationWriter is being used.
  //
  // |update_recipient| The ObservationWriter uses this to notify the update recipient when an
  // Observation has been added to the Observation Store. This must remain valid as long as the
  // ObservationWriter is being used.
  //
  // |observation_encrypter| This is used to encrypt Observations to the public key of Cobalt's
  // Analyzer prior to writing them into the Observation Store. If this value is equal to nullptr,
  // then Observations will not be encrypted before being added to the Observation Store. Otherwise,
  // this must remain valid as long as the ObservationWriter is being used.
  ObservationWriter(observation_store::ObservationStoreWriterInterface* observation_store,
                    observation_store::ObservationStoreUpdateRecipient* update_recipient,
                    util::EncryptedMessageMaker* observation_encrypter = nullptr)
      : observation_store_(observation_store),
        update_recipient_(update_recipient),
        observation_encrypter_(observation_encrypter) {}

  // Given an Observation |observation| and an ObservationMetadata |metadata|,
  // writes an encryption of the Observation together with the unencrypted
  // metadata to the Observation Store, and notifies the UpdateRecipient that an
  // Observation has been added to the store.
  [[nodiscard]] Status WriteObservation(const Observation2& observation,
                                        std::unique_ptr<ObservationMetadata> metadata) const;

 private:
  observation_store::ObservationStoreWriterInterface* observation_store_;
  observation_store::ObservationStoreUpdateRecipient* update_recipient_;
  util::EncryptedMessageMaker* observation_encrypter_;
};

}  // namespace logger
}  // namespace cobalt

#endif  // COBALT_SRC_LOGGER_OBSERVATION_WRITER_H_

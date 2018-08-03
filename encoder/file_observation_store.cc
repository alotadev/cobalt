// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctime>
#include <regex>
#include <utility>

#include "./logging.h"
#include "encoder/file_observation_store.h"
#include "encoder/file_observation_store_internal.pb.h"
#include "third_party/protobuf/src/google/protobuf/util/delimited_message_util.h"

namespace cobalt {
namespace encoder {

using tensorflow_statusor::StatusOr;
using util::Status;
using util::StatusCode;

const char kActiveFileName[] = "in_progress.data";
// The first part of the filename is 13 digits representing the milliseconds
// since the unix epoch. The millisecond timestamp became 13 digits in
// September 2001, and won't be 14 digits until 2286.
//
// The second part of the filename is 7 digits, which is a random number in the
// range 1000000-9999999.
const std::regex kFinalizedFileRegex(R"(\d{13}-\d{7}.data)");

FileObservationStore::FileObservationStore(
    size_t max_bytes_per_observation, size_t max_bytes_per_envelope,
    size_t max_bytes_total,
    std::unique_ptr<FileObservationStore::FileSystem> fs,
    const std::string &root_directory)
    : ObservationStore(max_bytes_per_observation, max_bytes_per_envelope,
                       max_bytes_total),
      fs_(std::move(fs)),
      root_directory_(root_directory),
      active_file_name_(FullPath(kActiveFileName)),
      random_int_(1000000, 9999999) {
  CHECK(fs_);

  // Check if root_directory_ already exists.
  if (!fs_->ListFiles(root_directory_).ok()) {
    // If it doesn't exist, create it here.
    // TODO(zmbush): If MakeDirectory doesn't work, we should fail over to
    // MemoryObservationStore.
    CHECK(fs_->MakeDirectory(root_directory_));
  }

  {
    auto fields = protected_fields_.lock();
    fields->finalized_bytes = 0;

    for (auto file : ListFinalizedFiles()) {
      fields->finalized_bytes +=
          fs_->FileSize(FullPath(file)).ConsumeValueOr(0);
    }

    // If there exists an active file, it likely means that the process
    // terminated unexpectedly last time. In this case, the file should be
    // finalized in order to be Taken from the store.
    //
    // For simplicity's sake, we attempt to the active_file_name_ while ignoring
    // the result. If the operation succeeds, then we rescued the active file.
    // Otherwise, there probably was no active file in the first place.
    FinalizeFile(active_file_name_, &fields);
  }
}

ObservationStore::StoreStatus FileObservationStore::AddEncryptedObservation(
    std::unique_ptr<EncryptedMessage> message,
    std::unique_ptr<ObservationMetadata> metadata) {
  auto fields = protected_fields_.lock();

  auto active_file = GetActiveFile(&fields);
  auto metadata_str = metadata->SerializeAsString();

  // "+1" below is for the |scheme| field of EncryptedMessage.
  size_t obs_size = message->ciphertext().size() +
                    message->public_key_fingerprint().size() + 1;
  if (obs_size > max_bytes_per_observation_) {
    VLOG(1) << "WARNING: An observation that was too big was passed in to "
               "FileObservationStore::AddEncryptedObservation(): "
            << obs_size;
    return kObservationTooBig;
  }

  size_t new_num_bytes = active_file->ByteCount() + obs_size;
  VLOG(4) << "new_num_bytes(" << new_num_bytes << ") > max_bytes_total_("
          << max_bytes_total_ << ")";
  if (new_num_bytes > max_bytes_total_) {
    VLOG(4) << "The observation store is full.";
    return kStoreFull;
  }

  if (!fields->metadata_written ||
      metadata_str != fields->last_written_metadata) {
    ObservationStoreRecord stored_metadata;
    stored_metadata.mutable_meta_data()->Swap(metadata.get());
    if (!google::protobuf::util::SerializeDelimitedToZeroCopyStream(
            stored_metadata, active_file)) {
      LOG(WARNING) << "Unable to write metadata to `" << active_file_name_
                   << "`";
      return kWriteFailed;
    }
    fields->metadata_written = true;
    fields->last_written_metadata = metadata_str;
  }

  ObservationStoreRecord stored_message;
  stored_message.mutable_encrypted_observation()->Swap(message.get());
  if (!google::protobuf::util::SerializeDelimitedToZeroCopyStream(
          stored_message, active_file)) {
    LOG(WARNING) << "Unable to write encrypted_observation to `"
                 << active_file_name_ << "`";
    return kWriteFailed;
  }

  if (active_file->ByteCount() >= (int64_t)max_bytes_per_envelope_) {
    VLOG(4) << "In-progress file contains " << active_file->ByteCount()
            << " bytes (>= " << max_bytes_per_envelope_ << "). Finalizing it.";

    if (!FinalizeFile(active_file_name_, &fields)) {
      LOG(WARNING) << "Unable to finalize `" << active_file_name_;
      return kWriteFailed;
    }
  }

  return kOk;
}

bool FileObservationStore::FinalizeFile(
    const std::string &filename,
    util::ProtectedFields<Fields>::LockedFieldsPtr *fields) {
  // Close the current file (if it is open).
  (*fields)->active_file = nullptr;
  (*fields)->active_fstream = nullptr;
  (*fields)->metadata_written = false;

  auto new_name = FullPath(GenerateFinalizedName());
  if (!fs_->Rename(filename, new_name)) {
    return false;
  }

  (*fields)->finalized_bytes += fs_->FileSize(new_name).ConsumeValueOr(0);
  return true;
}

std::string FileObservationStore::GenerateFinalizedName() const {
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();
  std::stringstream ss;
  ss << now << "-" << random_int_(random_dev_) << ".data";
  return ss.str();
}

std::string FileObservationStore::FullPath(const std::string &filename) const {
  return root_directory_ + "/" + filename;
}

std::string FileObservationStore::FileEnvelopeHolder::FullPath(
    const std::string &filename) const {
  return root_directory_ + "/" + filename;
}

google::protobuf::io::OstreamOutputStream *FileObservationStore::GetActiveFile(
    util::ProtectedFields<Fields>::LockedFieldsPtr *fields) {
  if ((*fields)->active_file == nullptr) {
    (*fields)->active_fstream.reset(
        new std::ofstream(active_file_name_, std::ofstream::out));
    (*fields)->active_file.reset(new google::protobuf::io::OstreamOutputStream(
        (*fields)->active_fstream.get()));
  }
  return (*fields)->active_file.get();
}

std::vector<std::string> FileObservationStore::ListFinalizedFiles() const {
  auto files = fs_->ListFiles(root_directory_).ConsumeValueOr({});
  std::vector<std::string> retval;
  for (auto file : files) {
    if (std::regex_match(file, kFinalizedFileRegex)) {
      retval.push_back(file);
    }
  }
  return retval;
}

StatusOr<std::string> FileObservationStore::GetOldestFinalizedFile() const {
  std::string found_file_name;
  auto fields = protected_fields_.const_lock();
  for (auto file : ListFinalizedFiles()) {
    if (fields->files_taken.find(file) == fields->files_taken.end()) {
      if (found_file_name == "") {
        found_file_name = file;
      } else {
        // We compare the file names to try to find the oldest one. This works
        // because file names are prefixed with the timestamp when they were
        // finalized and that timestamp is always 13 digits long.
        //
        // A lexigraphic order of fixed length number strings is identical to
        // ordering their numerical values.
        auto result = strcmp(file.c_str(), found_file_name.c_str());
        if (result < 0) {
          found_file_name = file;
        }
      }
    }
  }
  if (found_file_name == "") {
    return Status(StatusCode::NOT_FOUND, "No finalized file");
  }
  return found_file_name;
}

std::unique_ptr<ObservationStore::EnvelopeHolder>
FileObservationStore::TakeNextEnvelopeHolder() {
  auto oldest_file_name_or = GetOldestFinalizedFile();
  if (!oldest_file_name_or.ok()) {
    {
      auto fields = protected_fields_.lock();
      if (!fields->active_file || fields->active_file->ByteCount() == 0) {
        return nullptr;
      }
      FinalizeFile(active_file_name_, &fields);
    }
    oldest_file_name_or = GetOldestFinalizedFile();
    if (!oldest_file_name_or.ok()) {
      return nullptr;
    }
  }

  auto oldest_file_name = oldest_file_name_or.ConsumeValueOrDie();
  auto fields = protected_fields_.lock();
  fields->files_taken.insert(oldest_file_name);
  fields->finalized_bytes -=
      fs_->FileSize(FullPath(oldest_file_name)).ConsumeValueOr(0);
  return std::make_unique<FileEnvelopeHolder>(fs_.get(), root_directory_,
                                              oldest_file_name);
}

void FileObservationStore::ReturnEnvelopeHolder(
    std::unique_ptr<ObservationStore::EnvelopeHolder> envelope) {
  std::unique_ptr<FileObservationStore::FileEnvelopeHolder> env(
      static_cast<FileObservationStore::FileEnvelopeHolder *>(
          envelope.release()));

  auto fields = protected_fields_.lock();
  for (auto file_name : env->file_names()) {
    fields->files_taken.erase(file_name);
    fields->finalized_bytes +=
        fs_->FileSize(FullPath(file_name)).ConsumeValueOr(0);
  }
  env->clear();
}

size_t FileObservationStore::Size() const {
  auto fields = protected_fields_.const_lock();
  auto bytes = fields->finalized_bytes;
  if (fields->active_file) {
    bytes += fields->active_file->ByteCount();
  }
  VLOG(4) << "Size(): " << bytes;
  return bytes;
}

bool FileObservationStore::Empty() const { return Size() == 0; }

void FileObservationStore::Delete() {
  auto files = fs_->ListFiles(root_directory_).ConsumeValueOr({});
  for (auto file : files) {
    fs_->Delete(FullPath(file));
  }
  fs_->Delete(root_directory_);
}

FileObservationStore::FileEnvelopeHolder::~FileEnvelopeHolder() {
  for (auto file_name : file_names_) {
    fs_->Delete(FullPath(file_name));
  }
}

void FileObservationStore::FileEnvelopeHolder::MergeWith(
    std::unique_ptr<EnvelopeHolder> container) {
  std::unique_ptr<FileEnvelopeHolder> file_container(
      static_cast<FileEnvelopeHolder *>(container.release()));

  file_names_.insert(file_container->file_names_.begin(),
                     file_container->file_names_.end());

  file_container->file_names_.clear();

  envelope_.Clear();
  cached_file_size_ = 0;
  envelope_read_ = false;
}

const Envelope &FileObservationStore::FileEnvelopeHolder::GetEnvelope() {
  if (envelope_read_) {
    return envelope_;
  }

  std::string serialized_metadata;
  std::unordered_map<std::string, ObservationBatch *> batch_map;
  ObservationBatch *current_batch;

  ObservationStoreRecord stored;

  for (auto file_name : file_names_) {
    std::ifstream ifs(FullPath(file_name), std::ifstream::in);
    google::protobuf::io::IstreamInputStream iis(&ifs);

    bool clean_eof;
    while (google::protobuf::util::ParseDelimitedFromZeroCopyStream(
        &stored, &iis, &clean_eof)) {
      if (stored.has_meta_data()) {
        std::unique_ptr<ObservationMetadata> current_metadata(
            stored.release_meta_data());
        current_metadata->SerializeToString(&serialized_metadata);

        auto iter = batch_map.find(serialized_metadata);
        if (iter != batch_map.end()) {
          current_batch = iter->second;
        } else {
          current_batch = envelope_.add_batch();
          current_batch->set_allocated_meta_data(current_metadata.release());
          batch_map[serialized_metadata] = current_batch;
        }
      } else if (stored.has_encrypted_observation()) {
        current_batch->add_encrypted_observation()->Swap(
            stored.mutable_encrypted_observation());
      } else {
        clean_eof = false;
        break;
      }
    }

    if (!clean_eof) {
      VLOG(1)
          << "WARNING: Trying to read from `" << file_name
          << "` encountered a corrupted message. Returning the envelope that "
             "has been read so far.";
      break;
    }
  }

  envelope_read_ = true;
  return envelope_;
}

size_t FileObservationStore::FileEnvelopeHolder::Size() {
  if (cached_file_size_ > 0) {
    return cached_file_size_;
  }

  cached_file_size_ = 0;
  for (auto file_name : file_names_) {
    cached_file_size_ += fs_->FileSize(FullPath(file_name)).ConsumeValueOr(0);
  }
  return cached_file_size_;
}

}  // namespace encoder
}  // namespace cobalt

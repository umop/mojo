// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/url_response_disk_cache/url_response_disk_cache_impl.h"

#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "mojo/common/data_pipe_utils.h"
#include "mojo/public/cpp/application/application_connection.h"
#include "mojo/public/cpp/bindings/lib/fixed_buffer.h"
#include "services/url_response_disk_cache/url_response_disk_cache_entry.mojom.h"
#include "third_party/zlib/google/zip_reader.h"
#include "url/gurl.h"

namespace mojo {

namespace {

const char kEtagHeader[] = "etag";

template <typename T>
void Serialize(T input, std::string* output) {
  typedef typename mojo::internal::WrapperTraits<T>::DataType DataType;
  size_t size = GetSerializedSize_(input);
  mojo::internal::FixedBuffer buf(size);
  DataType data_type;
  Serialize_(input.Pass(), &buf, &data_type);
  std::vector<Handle> handles;
  data_type->EncodePointersAndHandles(&handles);
  void* serialized_data = buf.Leak();
  *output = std::string(static_cast<char*>(serialized_data), size);
  free(serialized_data);
}

template <typename T>
void Deserialize(std::string input, T* output) {
  typedef typename mojo::internal::WrapperTraits<T>::DataType DataType;
  DataType data_type = reinterpret_cast<DataType>(&input[0]);
  std::vector<Handle> handles;
  data_type->DecodePointersAndHandles(&handles);
  Deserialize_(data_type, output);
}

Array<uint8_t> PathToArray(const base::FilePath& path) {
  if (path.empty())
    return Array<uint8_t>();
  const std::string& string = path.value();
  Array<uint8_t> result(string.size());
  memcpy(&result.front(), string.data(), string.size());
  return result.Pass();
}

// Encode a string in ascii. This uses # as an escape character. It also escapes
// ':' because it is an usual path separator.
std::string EncodeString(const std::string& string) {
  std::string result = "";
  for (size_t i = 0; i < string.size(); ++i) {
    unsigned char c = string[i];
    if (c >= 32 && c < 128 && c != '#' && c != ':') {
      result += c;
    } else {
      result += base::StringPrintf("#%02x", c);
    }
  }
  return result;
}

// This service use a directory under HOME to store all of its data,
base::FilePath GetBaseDirectory() {
  return base::FilePath(getenv("HOME")).Append(".mojo_url_response_disk_cache");
}

// Returns the directory used store cached data for the given |url|, under
// |base_directory|.
base::FilePath GetDirName(base::FilePath base_directory,
                          const std::string& url) {
  // TODO(qsr): If the speed of directory traversal is problematic, this might
  // need to change to use less directories.
  return base_directory.Append(EncodeString(url));
}

// Returns the directory that the consumer can use to cache its own data.
base::FilePath GetConsumerCacheDirectory(const base::FilePath& main_cache) {
  return main_cache.Append("consumer_cache");
}

// Returns the path of the sentinel used to keep track of a zipped response has
// already been extracted.
base::FilePath GetExtractedSentinel(const base::FilePath& main_cache) {
  return main_cache.Append("extracted_sentinel");
}

// Runs the given callback. If |success| is false, call back with an error.
// Otherwise, store |entry| in |entry_path|, then call back with the given
// paths.
void RunCallbackWithSuccess(
    const URLResponseDiskCacheImpl::FilePathPairCallback& callback,
    const base::FilePath& content_path,
    const base::FilePath& cache_dir,
    const base::FilePath& entry_path,
    CacheEntryPtr entry,
    bool success) {
  if (!success) {
    callback.Run(base::FilePath(), base::FilePath());
    return;
  }
  std::string serialized_entry;
  Serialize(entry.Pass(), &serialized_entry);
  // We can ignore write error, as it will just force to clear the cache on the
  // next request.
  base::ImportantFileWriter::WriteFileAtomically(entry_path, serialized_entry);
  callback.Run(content_path, cache_dir);
}

// Run the given mojo callback with the given paths.
void RunMojoCallback(
    const Callback<void(Array<uint8_t>, Array<uint8_t>)>& callback,
    const base::FilePath& path1,
    const base::FilePath& path2) {
  callback.Run(PathToArray(path1), PathToArray(path2));
}

// Returns the list of values for the given |header_name| in the given list of
// headers.
std::vector<std::string> GetHeaderValues(const std::string& header_name,
                                         const Array<String>& headers) {
  std::vector<std::string> result;
  for (std::string header : headers.storage()) {
    if (StartsWithASCII(header, header_name, false)) {
      auto begin = header.begin();
      auto end = header.end();
      begin += header_name.size();
      // Extract the content of the header by finding the remaining string after
      // the ':' and stripping all spaces.
      while (begin < end && *begin != ':')
        begin++;
      if (begin < end) {
        begin++;
        while (begin < end && *begin == ' ')
          begin++;
        while (end > begin && *(end - 1) == ' ')
          end--;
        if (begin < end) {
          result.push_back(std::string(begin, end));
        }
      }
    }
  }
  return result;
}

// Returns whether the given directory |dir| constains a valid entry file for
// the given |response|. If this is the case and |output| is not |nullptr|, then
// the deserialized entry is returned in |*output|.
bool IsCacheEntryValid(const base::FilePath& dir,
                       URLResponse* response,
                       CacheEntryPtr* output) {
  // Find the entry file, and deserialize it.
  base::FilePath entry_path = dir.Append("entry");
  if (!base::PathExists(entry_path))
    return false;
  std::string serialized_entry;
  if (!ReadFileToString(entry_path, &serialized_entry))
    return false;
  CacheEntryPtr entry;
  Deserialize(serialized_entry, &entry);

  // If |entry| or |response| has not headers, it is not possible to check if
  // the entry is valid, so returns |false|.
  if (entry->headers.is_null() || response->headers.is_null())
    return false;

  // Only handle etag for the moment.
  std::string etag_header_name = kEtagHeader;
  std::vector<std::string> entry_etags =
      GetHeaderValues(etag_header_name, entry->headers);
  if (entry_etags.size() == 0)
    return false;
  std::vector<std::string> response_etags =
      GetHeaderValues(etag_header_name, response->headers);
  if (response_etags.size() == 0)
    return false;

  // Looking for the first etag header.
  bool result = entry_etags[0] == response_etags[0];

  // Returns |entry| if requested.
  if (output)
    *output = entry.Pass();

  return result;
}

}  // namespace

URLResponseDiskCacheImpl::URLResponseDiskCacheImpl(
    scoped_refptr<base::SequencedWorkerPool> worker_pool,
    const std::string& remote_application_url,
    InterfaceRequest<URLResponseDiskCache> request)
    : worker_pool_(worker_pool), binding_(this, request.Pass()) {
  base_directory_ = GetBaseDirectory();
  // The cached files are shared only for application of the same origin.
  if (remote_application_url != "") {
    base_directory_ = base_directory_.Append(
        EncodeString(GURL(remote_application_url).GetOrigin().spec()));
  }
}

URLResponseDiskCacheImpl::~URLResponseDiskCacheImpl() {
}

void URLResponseDiskCacheImpl::GetFile(URLResponsePtr response,
                                       const GetFileCallback& callback) {
  return GetFileInternal(response.Pass(),
                         base::Bind(&RunMojoCallback, callback));
}

void URLResponseDiskCacheImpl::GetExtractedContent(
    URLResponsePtr response,
    const GetExtractedContentCallback& callback) {
  base::FilePath dir = GetDirName(base_directory_, response->url);
  base::FilePath extracted_dir = dir.Append("extracted");
  if (IsCacheEntryValid(dir, response.get(), nullptr) &&
      PathExists(GetExtractedSentinel(dir))) {
    callback.Run(PathToArray(extracted_dir), PathToArray(dir));
    return;
  }

  GetFileInternal(
      response.Pass(),
      base::Bind(&URLResponseDiskCacheImpl::GetExtractedContentInternal,
                 base::Unretained(this), base::Bind(&RunMojoCallback, callback),
                 extracted_dir));
}

void URLResponseDiskCacheImpl::GetFileInternal(
    URLResponsePtr response,
    const FilePathPairCallback& callback) {
  base::FilePath dir = GetDirName(base_directory_, response->url);

  // Check if the response is cached and valid. If that's the case, returns the
  // cached value.
  CacheEntryPtr entry;
  if (IsCacheEntryValid(dir, response.get(), &entry)) {
    callback.Run(base::FilePath(entry->content_path),
                 GetConsumerCacheDirectory(dir));
    return;
  }

  // As the response was either not cached or the cached value is not valid, if
  // the cache directory for the response exists, it needs to be cleaned.
  if (base::PathExists(dir)) {
    base::FilePath to_delete;
    CHECK(CreateTemporaryDirInDir(base_directory_, "to_delete", &to_delete));
    CHECK(Move(dir, to_delete));
    worker_pool_->PostTask(
        FROM_HERE,
        base::Bind(base::IgnoreResult(&base::DeleteFile), to_delete, true));
  }

  // If the response has not a valid body, and it is not possible to create
  // either the cache directory or the consumer cache directory, returns an
  // error.
  if (!response->body.is_valid() ||
      !base::CreateDirectoryAndGetError(dir, nullptr) ||
      !base::CreateDirectoryAndGetError(GetConsumerCacheDirectory(dir),
                                        nullptr)) {
    callback.Run(base::FilePath(), base::FilePath());
    return;
  }

  // Fill the entry values for the request.
  base::FilePath entry_path = dir.Append("entry");
  base::FilePath content;
  CHECK(CreateTemporaryFileInDir(dir, &content));
  entry = CacheEntry::New();
  entry->url = response->url;
  entry->content_path = content.value();
  entry->headers = response->headers.Pass();
  // Asynchronously copy the response body to the cached file. The entry is send
  // to the callback so that it is saved on disk only if the copy of the body
  // succeded.
  common::CopyToFile(response->body.Pass(), content, worker_pool_.get(),
                     base::Bind(&RunCallbackWithSuccess, callback, content,
                                GetConsumerCacheDirectory(dir), entry_path,
                                base::Passed(entry.Pass())));
}

void URLResponseDiskCacheImpl::GetExtractedContentInternal(
    const FilePathPairCallback& callback,
    const base::FilePath& extracted_dir,
    const base::FilePath& content,
    const base::FilePath& dir) {
  // If it is not possible to get the cached file, returns an error.
  if (content.empty()) {
    callback.Run(base::FilePath(), base::FilePath());
    return;
  }

  // Unzip the content to the extracted directory. In case of any error, returns
  // an error.
  zip::ZipReader reader;
  if (!reader.Open(content)) {
    callback.Run(base::FilePath(), base::FilePath());
    return;
  }
  while (reader.HasMore()) {
    bool success = reader.OpenCurrentEntryInZip();
    success = success && reader.ExtractCurrentEntryIntoDirectory(extracted_dir);
    success = success && reader.AdvanceToNextEntry();
    if (!success) {
      callback.Run(base::FilePath(), base::FilePath());
      return;
    }
  }
  // We can ignore write error, as it will just force to clear the cache on the
  // next request.
  WriteFile(GetExtractedSentinel(dir), nullptr, 0);
  callback.Run(extracted_dir, GetConsumerCacheDirectory(dir));
}

}  // namespace mojo

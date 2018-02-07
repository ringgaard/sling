// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SLING_WEB_WEB_ARCHIVE_H_
#define SLING_WEB_WEB_ARCHIVE_H_

#include <string>

#include "sling/base/types.h"
#include "sling/stream/file-input.h"
#include "sling/stream/stream.h"
#include "sling/string/text.h"
#include "sling/web/rfc822-headers.h"

namespace sling {

// Process WARC (Web ARChive) input stream. A WARC file consists of a number of
// data blocks, e.g. web pages, with meta information.
class WARCInput {
 public:
  // Initialize WARC file input stream.
  WARCInput(InputStream *stream) : stream_(stream) {}
  ~WARCInput() { delete content_; }

  // Fetch next WARC data block. Return false if there are no more data blocks.
  bool Next();

  // WARC headers.
  const RFC822Headers &headers() const { return headers_; }
  Text uri() const { return uri_; }
  Text id() const { return id_; }
  Text type() const { return type_; }
  Text date() const { return date_; }
  Text content_type() const { return content_type_; }
  int content_length() const { return content_length_; }

  // Input stream for reading the content of the current data block.
  InputStream *content() const { return content_; }

 protected:
  // Input stream for reading WARC file.
  InputStream *stream() const { return stream_; }

 private:
  // Parse WARC header.
  bool ParseHeader();

  // Underlying input stream.
  InputStream *stream_;

  // WARC headers
  RFC822Headers headers_;

  // URI for current data block (WARC-Target-URI).
  Text uri_;

  // Record ID for current data block (WARC-Record-ID).
  Text id_;

  // Record type for current data block.
  Text type_;

  // Date for current data block.
  Text date_;

  // Content type for current data block.
  Text content_type_;

  // Content length for current data block.
  int64 content_length_ = 0;

  // Input stream with content for current data block.
  InputStream *content_ = nullptr;
};

// WARC file reader with decompression support.
class WARCFile : public WARCInput {
 public:
  WARCFile(const string &filename, int block_size = 1 << 20)
      : WARCInput(FileInput::Open(filename, block_size)) {}

  ~WARCFile() { delete stream(); }
};

}  // namespace sling

#endif  // STRING_WEB_WEB_ARCHIVE_H_


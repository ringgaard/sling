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

#ifndef SLING_WEB_RFC822_HEADERS_H_
#define SLING_WEB_RFC822_HEADERS_H_

#include <string>
#include <utility>
#include <vector>

#include "sling/base/types.h"
#include "sling/stream/input.h"
#include "sling/string/text.h"

namespace sling {

// Process RFC822 headers. RFC822 headers consists of a number of lines ending
// with an empty line. The first line is the "From" line, and the remaining
// lines are name/value pairs separated by colon.
class RFC822Headers : public std::vector<std::pair<Text, Text>> {
 public:
  // Parse RFC822 headers from input.
  bool Parse(Input *input);

  // Clear headers.
  void Clear();

  // Get value for header. Returns an empty text if the header is not found.
  Text Get(Text name) const;

  // First line of the header, i.e. the "From" line in RFC822.
  Text from() const { return from_; }

  // Return raw header buffer.
  const string &buffer() const { return buffer_; }

 private:
  // Header buffer.
  string buffer_;

  // First line of the header block.
  Text from_;
};

}  // namespace sling

#endif  // SLING_WEB_RFC822_HEADERS_H_


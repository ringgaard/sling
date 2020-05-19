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

#include "sling/web/rfc822-headers.h"

#include <strings.h>

#include "sling/string/strip.h"

namespace sling {

bool RFC822Headers::Parse(Input *input) {
  // Read until we find the end of the header, i.e. \r\n\r\n.
  Clear();
  int newlines = 0;
  char ch;
  bool skipws = true;
  while (newlines < 2) {
    if (!input->Next(&ch)) return false;
    if (skipws) {
      if (ch == '\r' || ch == '\n') continue;
      skipws = false;
    }
    buffer_.push_back(ch);
    if (ch == '\n') {
      newlines++;
    } else if (ch != '\r') {
      newlines = 0;
    }
  }

  // Parse header lines.
  // TODO: parse < uris >
  // TODO: parse "quoted \"strings\" with escapes"
  // See: https://iipc.github.io/warc-specifications/specifications/warc-format/warc-1.0/
  const char *p = buffer_.data();
  const char *end = p + buffer_.size();
  bool first = true;
  const char *start = p;
  const char *colon = nullptr;
  while (p < end) {
    if (*p == '\n') {
      if (first) {
        // Set "From" line.
        from_.set(start, p - start);
        StripWhiteSpace(&from_);
        first = false;
      } else {
        if (colon != nullptr) {
          // Add name value header pair.
          Text name(start, colon - start);
          Text value(colon + 1, p - colon - 1);
          StripWhiteSpace(&name);
          StripWhiteSpace(&value);
          emplace_back(name, value);
        } else {
          // Header line without colon. Add header with empty name.
          Text value(start, p - start);
          StripWhiteSpace(&value);
          if (!value.empty()) emplace_back(Text(), value);
        }
      }
      start = p;
      colon =  nullptr;
    } else if (*p == ':') {
      // Record the position of the first colon in the header line.
      if (colon == nullptr) colon = p;
    }
    p++;
  }
  return true;
}

void RFC822Headers::Clear() {
  clear();
  buffer_.clear();
  from_.clear();
}

Text RFC822Headers::Get(Text name) const {
  for (const auto &h : *this) {
    if (name.size() == h.first.size() &&
        strncasecmp(h.first.data(), name.data(), name.size())  == 0) {
      return h.second;
    }
  }
  return Text();
}

}  // namespace sling


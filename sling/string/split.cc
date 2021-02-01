// Copyright 2013 Google Inc. All Rights Reserved.
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

#include "sling/string/split.h"

#include <string.h>

#include "sling/base/logging.h"
#include "sling/base/macros.h"
#include "sling/base/types.h"

namespace sling {

namespace {

// This GenericFind() template function encapsulates the finding algorithm
// shared between the Literal and AnyOf delimiters. The FindPolicy template
// parameter allows each delimiter to customize the actual find function to use
// and the length of the found delimiter. For example, the Literal delimiter
// will ultimately use Text::find(), and the AnyOf delimiter will use
// Text::find_first_of().
template <typename FindPolicy>
Text GenericFind(
    Text text,
    Text delimiter,
    FindPolicy find_policy) {
  if (delimiter.empty() && text.length() > 0) {
    // Special case for empty string delimiters: always return a zero-length
    // Text referring to the item at position 1.
    return Text(text.begin() + 1, 0);
  }
  int found_pos = Text::npos;
  Text found(text.end(), 0);  // By default, not found
  found_pos = find_policy.Find(text, delimiter);
  if (found_pos != Text::npos) {
    found.set(text.data() + found_pos, find_policy.Length(delimiter));
  }
  return found;
}

// Finds using Text::find(), therefore the length of the found delimiter
// is delimiter.length().
struct LiteralPolicy {
  int Find(Text text, Text delimiter) {
    return text.find(delimiter);
  }
  int Length(Text delimiter) {
    return delimiter.length();
  }
};

// Finds using Text::find_first_of(), therefore the length of the found
// delimiter is 1.
struct AnyOfPolicy {
  int Find(Text text, Text delimiter) {
    return text.find_first_of(delimiter);
  }
  int Length(Text delimiter) {
    return 1;
  }
};

}  // namespace

//
// Literal
//

Literal::Literal(Text t) : delimiter_(t.str()) {}

Text Literal::Find(Text text) const {
  return GenericFind(text, delimiter_, LiteralPolicy());
}

//
// AnyOf
//

AnyOf::AnyOf(Text t) : delimiters_(t.str()) {}

Text AnyOf::Find(Text text) const {
  return GenericFind(text, delimiters_, AnyOfPolicy());
}

//
// FixedLength
//
FixedLength::FixedLength(int length) : length_(length) {
  CHECK_GT(length, 0);
}

Text FixedLength::Find(Text text) const {
  // If the string is shorter than the chunk size we say we
  // "can't find the delimiter" so this will be the last chunk.
  if (text.length() <= length_)
    return Text(text.end(), 0);

  return Text(text.begin() + length_, 0);
}

}  // namespace sling


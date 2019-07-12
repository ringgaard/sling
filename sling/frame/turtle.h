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

#ifndef SLING_FRAME_TURTLE_H_
#define SLING_FRAME_TURTLE_H_

#include <string>

#include "sling/frame/scanner.h"
#include "sling/stream/input.h"

namespace sling {

// Tokenizer for Turtle (Terse RDF Triple Language or TTL) syntax.
class TurtleTokenizer : public Scanner {
 public:
  // Token types in the range 0-255 are used for single-character tokens.
  enum TokenType {
    // Literal types.
    STRING_TOKEN = 260,
    INTEGER_TOKEN,
    DECIMAL_TOKEN,
    FLOAT_TOKEN,
    NAME_TOKEN,
    URI_TOKEN,

    // Multi-character tokens.
    TYPE_TOKEN,      // ^^
    IMPLIES_TOKEN,   // =>

    // Reserved keywords.
    A_TOKEN,
    TRUE_TOKEN,
    FALSE_TOKEN,
    PREFIX_TOKEN,
    BASE_TOKEN,
  };

  // Initializes tokenizer with input.
  explicit TurtleTokenizer(Input *input);

  // Reads the next input token.
  int NextToken();

  // Returns name prefix.
  const string &prefix() const { return prefix_; }

 private:
  // Parse <URI>.
  int ParseURI();

  // Parses string from input.
  int ParseString();

  // Parses number from input.
  int ParseNumber();

  // Parses name token from input. Returns false if symbol is invalid.
  bool ParseName();

  // Looks up keyword for the token in the token buffer. If this matches a
  // reserved keyword, it returns the keyword token number. Otherwise it is
  // treated as a name token.
  int LookupKeyword();

  // Name prefix for name tokens.
  string prefix_;
};

}  // namespace sling

#endif  // SLING_FRAME_TURTLE_H_


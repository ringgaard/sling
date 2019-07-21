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

#include <unordered_map>
#include <string>

#include "sling/frame/scanner.h"
#include "sling/frame/object.h"
#include "sling/frame/store.h"
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

  // Initialize tokenizer with input.
  explicit TurtleTokenizer(Input *input);

  // Read the next input token.
  int NextToken();

  // Return position of colon in prefixed name.
  int colon() const { return colon_; }

 private:
  // Parse <URI>.
  int ParseURI();

  // Parse string from input.
  int ParseString();

  // Parse number from input.
  int ParseNumber();

  // Look up keyword for the token in the token buffer. If this matches a
  // reserved keyword, it returns the keyword token number. Otherwise it is
  // treated as a name token.
  int LookupKeyword();

  // Position of colon in prefixed name.
  int colon_ = -1;
};

// Parser for Turtle (Terse RDF Triple Language or TTL) syntax.
class TurtleParser : public TurtleTokenizer {
 public:
  // Initialize parser with input.
  explicit TurtleParser(Store *store, Input *input)
    : TurtleTokenizer(input), store_(store),  stack_(store), tracking_(store) {}

  // Read next object from input.
  Object Read();

 private:
  // Read directive.
  void ReadDirective();

  // Read collection, i.e. array.
  Handle ReadCollection();

  // Read identifier. Return symbol if subject is true. Otherwise, an object
  // or proxy is returned.
  Handle ReadIdentifier(bool subject);

  // Object store for storing parsed objects.
  Store *store_;

  // Stack for storing intermediate objects while parsing.
  HandleSpace stack_;

  // Reference tracking for blank nodes, i.e anonymous frames.
  Handles tracking_;

  // Mapping from local names to blank nodes.
  HandleMap<string> locals_;

  // Base URI.
  string base_;

  // Name space map.
  std::unordered_map<string, string> namespaces_;
};

}  // namespace sling

#endif  // SLING_FRAME_TURTLE_H_

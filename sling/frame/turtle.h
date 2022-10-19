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
#include "sling/stream/output.h"

namespace sling {

// Tokenizer for Turtle (Terse RDF Triple Language or TTL) syntax.
class TurtleTokenizer : public Scanner {
 public:
  // Token types.
  enum TokenType {
    // Literal types.
    STRING_TOKEN = FIRST_AVAILABLE_TOKEN_TYPE,
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

  // Return prefix for name token.
  const string &prefix() const { return prefix_; }

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

  // Name prefix for name token.
  string prefix_;
};

// Parser for Turtle (Terse RDF Triple Language or TTL) syntax.
class TurtleParser : public TurtleTokenizer {
 public:
  // Initialize parser with input.
  explicit TurtleParser(Store *store, Input *input);

  // Read all objects from the input and return the last value.
  Object ReadAll();

  // Read next object from input.
  Object Read();

  // Read next object from input and return handle to it.
  Handle ReadObject();

 private:
  // Parse directive.
  bool ParseDirective();

  // Parse blank node as anonymous frame.
  Handle ParseBlankNode();

  // Parse predicate object list and push slots onto stack.
  bool ParsePredicateObjectList();

  // Parse collection as array.
  Handle ParseCollection();

  // Parse identifier.
  Handle ParseIdentifier(bool subject);

  // Parse predicate.
  Handle ParsePredicate();

  // Parse value.
  Handle ParseValue();

  // Get the current location in the handle stack.
  Word Mark() { return stack_.offset(stack_.end()); }

  // Pop elements off the stack.
  void Release(Word mark) { stack_.set_end(stack_.address(mark)); }

  // Push value onto stack.
  void Push(Handle h) { *stack_.push() = h; }

  // Push frame slots onto stack.
  void PushFrame(FrameDatum *frame) {
    for (Slot *s = frame->begin(); s < frame->end(); ++s) {
      Push(s->name);
      Push(s->value);
    }
  }

  // Check if URI is relative.
  static bool IsRelativeURI(const string &uri);

  // Object store for storing parsed objects.
  Store *store_;

  // Stack for storing intermediate objects while parsing.
  HandleSpace stack_;

  // Reference tracking for blank nodes, i.e anonymous frames.
  Handles references_;

  // Mapping from local name to index of blank node reference.
  std::unordered_map<string, int> locals_;

  // Base URI.
  string base_;

  // Namespace map for prefixed names.
  std::unordered_map<string, string> namespaces_;
};

// The Turtle writer outputs objects in RDF Turtle format.
class TurtleWriter {
 public:
  // Initializes writer with store and output.
  TurtleWriter(const Store *store, Output *output)
      : store_(store), output_(output) {}

  // Write object on output.
  void Write(const Object &object);
  void Write(Handle handle, bool reference = false);

  // Configuration parameters.
  void set_indent(int indent) { indent_ = indent; }

 private:
  // Returns true if the output should be pretty-printed.
  bool pretty() const { return indent_ > 0; }

  // Writes character on output.
  void WriteChar(char ch) {
    output_->WriteChar(ch);
  }

  // Writes two character on output.
  void WriteChars(char ch1, char ch2) {
    output_->WriteChar(ch1);
    output_->WriteChar(ch2);
  }

  // Writes string.
  void WriteString(const StringDatum *str);

  // Writes frame.
  void WriteFrame(const FrameDatum *frame, bool reference);

  // Writes array.
  void WriteArray(const ArrayDatum *array);

  // Writes symbol.
  void WriteSymbol(const SymbolDatum *symbol);

  // Writes integer.
  void WriteInt(int number);

  // Writes floating-point number.
  void WriteFloat(float number);

  // Object store.
  const Store *store_;

  // Output for writer.
  Output *output_;

  // Amount by which output would be indented. Zero means no indentation.
  int indent_ = 0;

  // Current indentation (number of spaces).
  int current_indentation_ = 0;

  DISALLOW_IMPLICIT_CONSTRUCTORS(TurtleWriter);
};

}  // namespace sling

#endif  // SLING_FRAME_TURTLE_H_

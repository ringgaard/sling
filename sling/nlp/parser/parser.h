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

#ifndef SLING_NLP_PARSER_PARSER_H_
#define SLING_NLP_PARSER_PARSER_H_

#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sling/base/logging.h"
#include "sling/base/registry.h"
#include "sling/base/types.h"
#include "sling/file/file.h"
#include "sling/frame/store.h"
#include "sling/myelin/compiler.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/flow.h"
#include "sling/nlp/document/document.h"
#include "sling/nlp/document/lexical-encoder.h"
#include "sling/nlp/parser/action-table.h"
#include "sling/nlp/parser/cascade.h"
#include "sling/nlp/parser/parser-features.h"
#include "sling/nlp/parser/parser-state.h"
#include "sling/nlp/parser/roles.h"
#include "sling/nlp/parser/trace.h"

namespace sling {
namespace nlp {

class ParserInstance;

// Frame semantics parser model.
class Parser {
 public:
  // Load and initialize parser model.
  void Load(Store *store, const string &filename);

  // Parse document.
  void Parse(Document *document) const;

  // Neural network for parser.
  const myelin::Network &network() const { return network_; }

  // Return the lexical encoder.
  const LexicalEncoder &encoder() const { return encoder_; }

  // Returns whether tracing is enabled.
  bool trace() const { return trace_; }

  // Enable/disable tracing.
  void set_trace(bool trace) { trace_ = trace; }

 private:
  // JIT compiler.
  myelin::Compiler compiler_;

  // Parser network.
  myelin::Network network_;

  // Lexical encoder.
  LexicalEncoder encoder_;

  // Parser decoder.
  myelin::Cell *decoder_ = nullptr;

  // Parser feature model for feature extraction in the decoder.
  ParserFeatureModel feature_model_;

  // Cascade.
  Cascade cascade_;

  // Global store for parser.
  Store *store_ = nullptr;

  // Parser action table.
  ActionTable actions_;

  // Set of roles considered.
  RoleSet roles_;

  // Whether tracing is enabled.
  bool trace_;

  friend class ParserInstance;
};

// Parser state for running an instance of the parser on a document.
class ParserInstance {
 public:
  ParserInstance(const Parser *parser, Document *document, int begin, int end);
  ~ParserInstance() { delete trace_; }

  // Attach channels for decoder.
  void AttachChannels(const myelin::BiChannel &bilstm) {
    features_.Attach(bilstm, &activations_, &decoder_);
  }

  // Extract features for decoder.
  void ExtractDecoderFeatures() {
    features_.Extract(&decoder_);
    if (trace_ != nullptr) features_.TraceFeatures(&decoder_, trace_);
  }

 private:
  // Parser model.
  const Parser *parser_;

  // Instance for lexical encoding computation.
  LexicalEncoderInstance encoder_;

  // Parser transition state.
  ParserState state_;

  // Decoder feature extractor.
  ParserFeatureExtractor features_;

  // Decoder instance for computing decoder activations.
  myelin::Instance decoder_;

  // Decoder step activations.
  myelin::Channel activations_;

  // Instance for cascade computations.
  CascadeInstance cascade_ = nullptr;

  // Trace information.
  Trace *trace_;

  friend class Parser;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_PARSER_PARSER_H_


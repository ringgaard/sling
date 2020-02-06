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

#include <string>
#include <utility>
#include <vector>

#include "sling/base/types.h"
#include "sling/frame/store.h"
#include "sling/myelin/compiler.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/flow.h"
#include "sling/nlp/document/document.h"
#include "sling/nlp/parser/parser-codec.h"

namespace sling {
namespace nlp {

// Frame semantics parser.
class Parser {
 public:
  // List of hyperparameter names and values.
  typedef std::vector<std::pair<string, string>> HyperParams;

  ~Parser();

  // Load and initialize parser model.
  void Load(Store *store, const string &filename);

  // Parse document.
  void Parse(Document *document) const;

  // Neural network model for parser.
  const myelin::Network &model() const { return model_; }

  // Hyperparameters for parser model.
  const HyperParams &hparams() const { return hparams_; }

 private:
  // JIT compiler.
  myelin::Compiler compiler_;

  // Parser network.
  myelin::Network model_;

  // Parser encoder.
  ParserEncoder *encoder_ = nullptr;

  // Parser encoder.
  ParserDecoder *decoder_ = nullptr;

  // Hyperparameters for parser model.
  HyperParams hparams_;

  // Sentence skip mask. Default to skipping headings.
  int skip_mask_ = HEADING_BEGIN;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_PARSER_PARSER_H_


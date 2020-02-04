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

#ifndef SLING_NLP_PARSER_ENCODER_H_
#define SLING_NLP_PARSER_ENCODER_H_

#include <vector>

#include "sling/base/registry.h"
#include "sling/frame/object.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/flow.h"
#include "sling/nlp/document/document.h"
#include "sling/task/task.h"
#include "sling/util/vocabulary.h"

namespace sling {
namespace nlp {

class EncoderInstance;
class EncoderLearner;

// An encoder transforms a sentence to an embedding representation for each
// token in the sentence.
class Encoder : public Component<Encoder> {
 public:
  virtual ~Encoder() = default;

  // Set up encoder for training task.
  virtual void Setup(task::Task *task) = 0;

  // Build flow for encoder. Returns the output variable for token encoding.
  virtual myelin::Flow::Variable *Build(myelin::Flow *flow,
                                        Vocabulary::Iterator *words,
                                        bool learn) = 0;

  // Save encoder model.
  virtual void Save(myelin::Flow *flow, Builder *spec) = 0;

  // Load encoder model.
  virtual void Load(myelin::Flow *flow, const Frame &spec) = 0;

  // Initialize encoder model.
  virtual void Initialize(const myelin::Network &net) = 0;

  // Create encoder instance.
  virtual EncoderInstance *CreateInstance() = 0;

  // Create encoder learner.
  virtual EncoderLearner *CreateLearner() = 0;
};

#define REGISTER_ENCODER(type, component) \
    REGISTER_COMPONENT_TYPE(sling::nlp::Encoder, type, component)

// An encoder instance can transform a sentence in a document to an
// embedding representation for each token.
class EncoderInstance {
 public:
  virtual ~EncoderInstance() = default;

  // Compute token embeddings for sentence.
  virtual myelin::Channel *Compute(const Document &document,
                                   int begin, int end) = 0;
};

// Encoder learner instance.
class EncoderLearner {
 public:
  virtual ~EncoderLearner() = default;

  // Compute token embedding for tokens in sentence.
  virtual myelin::Channel *Compute(const Document &document,
                                   int begin, int end) = 0;

  // Backpropagate gradients through encoder.
  virtual void Backpropagate(myelin::Channel *doutput) = 0;

  // Collect gradients.
  virtual void CollectGradients(std::vector<myelin::Instance *> *gradients) = 0;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_PARSER_ENCODER_H_


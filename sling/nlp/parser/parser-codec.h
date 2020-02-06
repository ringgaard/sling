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

#ifndef SLING_NLP_PARSER_PARSER_CODEC_H_
#define SLING_NLP_PARSER_PARSER_CODEC_H_

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

// An encoder transforms a sentence in a document to an embedding representation
// for each token in the sentence. This is the lower parser in a
// sequence-to-sequence model.
class ParserEncoder : public Component<ParserEncoder> {
 public:
  virtual ~ParserEncoder() = default;

  // Set up encoder for leraning.
  virtual void Setup(task::Task *task, Store *commons) = 0;

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

  // Predictor instance for transforming a sentence to an embedding
  // representation.
  class Predictor {
   public:
    virtual ~Predictor() = default;

    // Compute token embeddings for sentence.
    virtual myelin::Channel *Encode(const Document &document,
                                    int begin, int end) = 0;
  };

  // Create predictor instance.
  virtual Predictor *CreatePredictor() = 0;

  // Learner instance for encoding model.
  class Learner : public Predictor {
   public:
    // Backpropagate gradients through encoder.
    virtual void Backpropagate(myelin::Channel *doutput) = 0;

    // Collect gradients.
    typedef std::vector<myelin::Instance *> Gradients;
    virtual void CollectGradients(Gradients *gradients) = 0;
  };

  // Create learner learner.
  virtual Learner *CreateLearner() = 0;
};

#define REGISTER_PARSER_ENCODER(type, component) \
    REGISTER_COMPONENT_TYPE(sling::nlp::ParserEncoder, type, component)

// An decoder takes sentences represented as sequences of token embeddings and
// decodes these into annotations on the document. This is the upper part in a
// sequence-to-sequence model.
class ParserDecoder : public Component<ParserDecoder> {
 public:
  virtual ~ParserDecoder() = default;

  // Set up decoder for learning.
  virtual void Setup(task::Task *task, Store *commons) = 0;

  // Build flow for decoder.
  virtual void Build(myelin::Flow *flow,
                     myelin::Flow::Variable *encodings,
                     bool learn) = 0;

  // Save decoder model.
  virtual void Save(myelin::Flow *flow, Builder *spec) = 0;

  // Load decoder model.
  virtual void Load(myelin::Flow *flow, const Frame &spec) = 0;

  // Initialize decoder model.
  virtual void Initialize(const myelin::Network &net) = 0;

  // Predictor instance for decoder model.
  class Predictor {
   public:
    virtual ~Predictor() = default;

    // Switch to new document.
    virtual void Switch(Document *document) = 0;

    // Decode document part based on input encodings and add the predicted
    // annotations to the current document.
    virtual void Decode(int begin, int end, myelin::Channel *encodings) = 0;
  };

  // Create predictor instance.
  virtual Predictor *CreatePredictor() = 0;

  // Learner instance for decoder model.
  class Learner {
   public:
    virtual ~Learner() = default;

    // Switch to new document.
    virtual void Switch(Document *document) = 0;

    // Learn decoder annotations for document part with the token encodings as
    // input and token gradients as output.
    virtual myelin::Channel *Learn(int begin, int end,
                                   myelin::Channel *encodings) = 0;

    // Accumulate loss from learned instances.
    virtual void UpdateLoss(float *loss_sum, int *loss_count) = 0;

    // Collect gradients.
    typedef std::vector<myelin::Instance *> Gradients;
    virtual void CollectGradients(Gradients *gradients) = 0;
  };

  // Create learner learner.
  virtual Learner *CreateLearner() = 0;
};

#define REGISTER_PARSER_DECODER(type, component) \
    REGISTER_COMPONENT_TYPE(sling::nlp::ParserDecoder, type, component)

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_PARSER_PARSER_CODEC_H_


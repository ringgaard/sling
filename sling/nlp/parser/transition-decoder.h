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

#ifndef SLING_NLP_PARSER_TRANSITION_DECODER_H_
#define SLING_NLP_PARSER_TRANSITION_DECODER_H_

#include "sling/nlp/parser/parser-action.h"
#include "sling/nlp/parser/parser-codec.h"
#include "sling/nlp/parser/parser-features.h"
#include "sling/nlp/parser/roles.h"

namespace sling {
namespace nlp {

class TransitionDecoder : public ParserDecoder {
 public:
  // Decoder interface.
  void Setup(task::Task *task) override;
  void Build(myelin::Flow *flow, 
             myelin::Flow::Variable *encodings, 
             bool learn) override;
  void Save(myelin::Flow *flow, Builder *spec) override;
  void Load(myelin::Flow *flow, const Frame &spec) override;
  void Initialize(const myelin::Network &net) override;

  // Decoder predictor.
  class TransitionDecoderPredictor : public Predictor {
   public:
    TransitionDecoderPredictor(const TransitionDecoder *decoder) {}
  };

  Predictor *CreatePredictor() override { 
    return new TransitionDecoderPredictor(this); 
  }
  
  // Decoder learner.
  class TransitionDecoderLearner : public Learner {
   public:
    TransitionDecoderLearner(const TransitionDecoder *encoder) {}
  };

  Learner *CreateLearner() override { 
    return new TransitionDecoderLearner(this); 
  }

 private:
  // Model hyperparameters.
  int mark_depth_ = 1;
  int frame_limit_ = 5;
  int history_size_ = 5;
  int out_roles_size_ = 32;
  int in_roles_size_ = 32;
  int labeled_roles_size_ = 32;
  int unlabeled_roles_size_ = 32;
  int roles_dim_ = 16;
  int activations_dim_ = 128;
  int link_dim_token_ = 32;
  int link_dim_step_ = 64;
  int mark_dim_ = 32;
  float ff_l2reg_ = 0.0;

  // Decoder model.
  myelin::Cell *decoder_ = nullptr;
  myelin::Tensor *encodings_ = nullptr;
  myelin::Tensor *activations_ = nullptr;
  myelin::Tensor *activation_ = nullptr;

  myelin::Cell *gdecoder_ = nullptr;
  myelin::Tensor *primal_ = nullptr;
  myelin::Tensor *dencodings_ = nullptr;
  myelin::Tensor *dactivations_ = nullptr;
  myelin::Tensor *dactivation_ = nullptr;

  // Role set.
  RoleSet roles_;

  // Parser feature model.
  ParserFeatureModel feature_model_;

  // Reset parser state between sentences in a document.
  bool sentence_reset_ = true;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_PARSER_TRANSITION_DECODER_H_


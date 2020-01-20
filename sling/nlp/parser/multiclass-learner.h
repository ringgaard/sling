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

#ifndef SLING_NLP_PARSER_MULTICLASS_LEARNER_H_
#define SLING_NLP_PARSER_MULTICLASS_LEARNER_H_

#include <string>
#include <vector>

#include "sling/base/types.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/flow.h"
#include "sling/myelin/learning.h"
#include "sling/nlp/parser/action-table.h"
#include "sling/nlp/parser/parser-trainer.h"

namespace sling {
namespace nlp {

// Delegate for fixed action classification using a softmax cross-entropy
// loss.
class MultiClassDelegateLearner : public DelegateLearner {
 public:
  MultiClassDelegateLearner(const string &name)
      : name_(name), loss_(name + "_loss") {}

  // Delegate learner interface.
  void Build(myelin::Flow *flow,
             myelin::Flow::Variable *activation,
             myelin::Flow::Variable *dactivation,
             bool learn) override;
  void Initialize(const myelin::Network &network) override;
  DelegateLearnerInstance *CreateInstance() override;
  void Save(myelin::Flow *flow, Builder *spec) override;

  // Multi-class delegate instance.
  class DelegateInstance : public DelegateLearnerInstance {
   public:
    DelegateInstance(MultiClassDelegateLearner *learner)
        : learner_(learner),
          forward_(learner->cell_),
          backward_(learner->dcell_) {}

    // Delegate learner instance interface.
    void CollectGradients(std::vector<myelin::Instance *> *gradients) override;
    void ClearGradients() override;
    float Compute(float *activation,
                  float *dactivation,
                  const ParserAction &action) override;
    void Predict(float *activation, ParserAction *action) override;

   private:
    MultiClassDelegateLearner *learner_;
    myelin::Instance forward_;
    myelin::Instance backward_;
  };

 protected:
  string name_;                        // delegate name
  ActionTable actions_;                // action table with outcomes
  myelin::CrossEntropyLoss loss_;      // loss function

  myelin::Cell *cell_ = nullptr;       // cell for forward computation
  myelin::Tensor *input_ = nullptr;    // input for activations
  myelin::Tensor *logits_ = nullptr;   // logits for actions
  myelin::Tensor *output_ = nullptr;   // output prediction

  myelin::Cell *dcell_ = nullptr;      // cell for backward computation
  myelin::Tensor *primal_ = nullptr;   // primal reference
  myelin::Tensor *dinput_ = nullptr;   // gradient for activations
  myelin::Tensor *dlogits_ = nullptr;  // gradient for logits
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_PARSER_MULTICLASS_LEARNER_H_


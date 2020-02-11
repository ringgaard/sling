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

#ifndef SLING_NLP_PARSER_DELEGATE_H_
#define SLING_NLP_PARSER_DELEGATE_H_

#include <vector>

#include "sling/base/registry.h"
#include "sling/base/types.h"
#include "sling/frame/store.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/flow.h"
#include "sling/nlp/parser/parser-action.h"

namespace sling {
namespace nlp {

// A delegate predicts parser actions from the activation output of the
// transition decoder.
class Delegate : public Component<Delegate> {
 public:
  virtual ~Delegate() = default;

  // Build flow for delegate.
  virtual void Build(myelin::Flow *flow,
                     myelin::Flow::Variable *activation,
                     myelin::Flow::Variable *dactivation,
                     bool learn) = 0;

  // Save delegate to flow.
  virtual void Save(myelin::Flow *flow, Builder *spec) = 0;

  // Load delegate from flow.
  virtual void Load(myelin::Flow *flow, const Frame &spec) = 0;

  // Initialize delegate from model.
  virtual void Initialize(const myelin::Network &model) = 0;

  // Interface for delegate instance at prediction time.
  class Predictor {
   public:
    virtual ~Predictor() = default;

    // Predict action for delegate.
    virtual void Predict(float *activations, ParserAction *action) = 0;
  };

  // Create new delegate predictor.
  virtual Predictor *CreatePredictor() = 0;

  // Interface for delegate learner.
  class Learner : public Predictor {
   public:
    // Compute loss and gradient for delegate with respect to golden action.
    virtual float Compute(float *activation,
                          float *dactivation,
                          const ParserAction &action) = 0;
    // Collect gradients.
    virtual void CollectGradients(myelin::Instances *gradients) = 0;
  };

  // Create new delegate learner.
  virtual Learner *CreateLearner() = 0;
};

#define REGISTER_DELEGATE(type, component) \
    REGISTER_COMPONENT_TYPE(sling::nlp::Delegate, type, component)

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_PARSER_DELEGATE_H_


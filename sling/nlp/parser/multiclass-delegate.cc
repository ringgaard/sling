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

#include "sling/nlp/parser/multiclass-delegate.h"

#include "sling/myelin/gradient.h"

namespace sling {
namespace nlp {

using namespace myelin;

void MultiClassDelegate::Build(Flow *flow,
                               Flow::Variable *activation,
                               Flow::Variable *dactivation,
                               bool learn) {
  FlowBuilder f(flow, name_);
  int dim = activation->elements();
  int size = actions_.size();
  auto *W = f.Parameter("W", DT_FLOAT, {dim, size});
  auto *b = f.Parameter("b", DT_FLOAT, {1, size});
  f.RandomNormal(W);

  auto *input = f.Placeholder("input", DT_FLOAT, {1, dim}, true);
  auto *logits = f.Name(f.Add(f.MatMul(input, W), b), "logits");
  if (learn) logits->set_out();
  auto *output = f.Name(f.ArgMax(logits), "output");
  if (!learn) output->set_out();

  flow->Connect({activation, input});
  if (learn) {
    Gradient(flow, f.func());
    auto *dlogits = flow->GradientVar(logits);
    loss_.Build(flow, logits, dlogits);
  }
}

void MultiClassDelegate::Save(myelin::Flow *flow, Builder *spec) {
  spec->Add("name", name_);
  spec->Add("type", "multiclass");
  spec->Add("cell", cell_->name());
  actions_.Write(spec);
}

void MultiClassDelegate::Load(myelin::Flow *flow, const Frame &spec) {
  name_ = spec.GetString("cell");
  actions_.Read(spec);
}

void MultiClassDelegate::Initialize(const Network &model) {
  cell_ = model.GetCell(name_);
  input_ = cell_->GetParameter(name_ + "/input");
  logits_ = cell_->GetParameter(name_ + "/logits");
  output_ = cell_->GetParameter(name_ + "/output");

  dcell_ = cell_->Gradient();
  if (dcell_ != nullptr) {
    primal_ = cell_->Primal();
    dinput_ = input_->Gradient();
    dlogits_ = logits_->Gradient();
    loss_.Initialize(model);
  }
}

void MultiClassDelegate::Predictor::Predict(float *activation,
                                            ParserAction *action) {
  // Predict action from activations.
  data_.SetReference(delegate_->input_, activation);
  data_.Compute();
  int argmax = *data_.Get<int>(delegate_->output_);
  *action = delegate_->actions_.Action(argmax);
}

void MultiClassDelegate::Learner::Predict(float *activation,
                                          ParserAction *action) {
  // Predict action from activations.
  forward_.SetReference(delegate_->input_, activation);
  forward_.Compute();
  int argmax = *forward_.Get<int>(delegate_->output_);
  *action = delegate_->actions_.Action(argmax);
}

float MultiClassDelegate::Learner::Compute(float *activation,
                                           float *dactivation,
                                           const ParserAction &action) {
  // Look up index for action. Skip backpropagation if action is unknown.
  int target = delegate_->actions_.Index(action);
  if (target == -1) return 0.0;

  // Compute logits from activation.
  forward_.SetReference(delegate_->input_, activation);
  forward_.Compute();

  // Compute loss.
  float *logits = forward_.Get<float>(delegate_->logits_);
  float *dlogits = backward_.Get<float>(delegate_->dlogits_);
  float loss = delegate_->loss_.Compute(logits, target, dlogits);

  // Backpropagate loss.
  backward_.Set(delegate_->primal_, &forward_);
  backward_.SetReference(delegate_->dinput_, dactivation);
  backward_.Compute();

  return loss;
}

void MultiClassDelegate::Learner::CollectGradients(Gradients *gradients) {
  gradients->push_back(&backward_);
}

REGISTER_DELEGATE("multiclass", MultiClassDelegate);

}  // namespace nlp
}  // namespace sling


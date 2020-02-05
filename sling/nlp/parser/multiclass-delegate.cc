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
  // TODO
}

void MultiClassDelegate::Load(myelin::Flow *flow, const Frame &spec) {
  name_ = spec.GetString("cell");
  actions_.Read(spec);
}

void MultiClassDelegate::Initialize(const Network &net) {
  cell_ = net.GetCell(name_);
  input_ = cell_->GetParameter(name_ + "/input");
  logits_ = cell_->GetParameter(name_ + "/logits");
  output_ = cell_->GetParameter(name_ + "/output");

  dcell_ = cell_->Gradient();
  if (dcell_ != nullptr) {
    primal_ = cell_->Primal();
    dinput_ = input_->Gradient();
    dlogits_ = logits_->Gradient();
    loss_.Initialize(net);
  }
}

REGISTER_DELEGATE("multiclass", MultiClassDelegate);

}  // namespace nlp
}  // namespace sling


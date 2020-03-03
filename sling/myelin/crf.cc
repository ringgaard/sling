// Copyright 2018 Google Inc.
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

#include "sling/myelin/crf.h"

#include "sling/myelin/builder.h"
#include "sling/myelin/gradient.h"

namespace sling {
namespace myelin {

CRF::Variables CRF::Build(Flow *flow, Flow::Variable *input, bool learn) {
  // TODO: compute both partition function and sentence score in the same
  // step cell and compute the final scores in a loss cell. The step cell takes
  // the input emissions, the previous alpha, and the tag pair (next, prev)
  // as input.
  Variables vars;
  auto dt = input->type;
  int num_labels = input->dim(1) + 2;

  // Build function for computing one step of the forward pass of the
  // partition function.
  FlowBuilder fwd(flow, name_ + "/forward");

  // Build inputs: the unary score and the alpha from the previous step.
  vars.input = fwd.Placeholder("input", dt, input->shape, true);
  vars.alpha_in = fwd.Placeholder("alpha_in", dt, {1, num_labels}, true);
  flow->Connect({input, vars.input});
  vars.input->set_unique();
  vars.alpha_in->set_unique();

  // Add BOS and EOS labels to input emissions.
  auto *padding = fwd.Const(nullptr, dt, {1, 2});
  auto *emissions = fwd.Name(fwd.Concat({vars.input, padding}, 1), "emissions");

  // Transition weights (next, prev).
  auto *transitions = fwd.Parameter("transitions", dt, {num_labels, num_labels});

  // Compute scores.
  auto *scores = fwd.Add(fwd.Add(emissions, vars.alpha_in), transitions);

  // Compute alpha_out.
  vars.alpha_out = fwd.Name(fwd.LogSumExp(scores, 0, true), "alpha_out");
  vars.alpha_out->set_out()->set_ref();

  // Create empty input element.
  auto *empty = flow->AddConstant(name_ + "/empty", input->type, input->shape);
  empty->set_out();
  flow->Connect({input, empty});

  // Build gradients for learning.
  if (learn) {
    Gradient(flow, fwd.func());
    vars.dinput = flow->GradientVar(vars.input);
    vars.beta_in = flow->GradientVar(vars.alpha_out);
    vars.beta_out = flow->GradientVar(vars.alpha_in);
  }

  return vars;
}

void CRF::Initialize(const Network &net) {
}

}  // namespace myelin
}  // namespace sling


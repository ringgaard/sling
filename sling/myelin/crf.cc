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

CRF::Variables CRF::Build(Flow *flow,
                          Flow::Variable *input,
                          Flow::Variable *dinput) {
  Variables vars;
  auto dt = input->type;
  int num_labels = input->dim(1) + 2;
  bool learn = dinput != nullptr;

  // Build function for one step of computing parition function and score.
  FlowBuilder f(flow, name_ + "/step");

  // Build inputs.
  vars.input = f.Placeholder("input", dt, input->shape, true);
  vars.alpha_in = f.Placeholder("alpha_in", dt, {1, num_labels}, true);
  vars.input->set_unique();
  vars.alpha_in->set_unique();
  vars.prev = f.Placeholder("prev", DT_INT32, {1});
  vars.curr = f.Placeholder("curr", DT_INT32, {1});

  // Add BOS and EOS labels to input emissions.
  auto *padding = f.Const(nullptr, dt, {1, 2});
  auto *emissions = f.Name(f.Concat({vars.input, padding}, 1), "emissions");

  // Transition weights (prev, curr).
  auto *transitions = f.Parameter("transitions", dt, {num_labels, num_labels});

  // Compute scores.
  auto *scores = f.Add(f.Add(emissions, vars.alpha_in), transitions);

  // Compute alpha_out.
  vars.alpha_out = f.Name(f.LogSumExp(scores, 0, true), "alpha_out");
  vars.alpha_out->set_out()->set_ref();

  // Compute score for element.
  std::vector<int> one{1};
  auto *e_index = f.Concat({f.Const(one), vars.curr});
  auto *e_score = f.Gather(emissions, e_index);
  auto *t_index = f.Concat({vars.prev, vars.curr});
  auto *t_score = f.Gather(transitions, t_index);
  vars.score = f.Name(f.Add(t_score, e_score), "score");
  vars.score->set_out();

  // Build likelihood function.
  FlowBuilder fl(flow, name_ + "/likelihood");
  auto *score = fl.Placeholder("score", dt, {});
  auto *alpha = fl.Placeholder("alpha", dt, {1, num_labels}, true);
  alpha->set_unique();
  auto *p = fl.Name(fl.Sub(score, fl.LogSumExp(alpha)), "p");
  p->set_out();

  // Create empty input element.
  auto *empty = flow->AddConstant(name_ + "/empty", input->type, input->shape);
  empty->set_out();
  flow->Connect({input, empty});

  // Connect variables.
  flow->Connect({input, vars.input});
  flow->Connect({vars.alpha_out, vars.alpha_in, alpha});

  // Build gradients for learning.
  if (learn) {
    Gradient(flow, f.func());
    Gradient(flow, fl.func());
    vars.dinput = flow->GradientVar(vars.input);
    vars.beta_in = flow->GradientVar(vars.alpha_out);
    vars.beta_out = flow->GradientVar(vars.alpha_in);
    flow->Connect({dinput, vars.dinput});
  }

  // Build loss function.
  loss_.Build(flow);

  return vars;
}

void CRF::Initialize(const Network &net) {
  loss_.Initialize(net);
}

}  // namespace myelin
}  // namespace sling


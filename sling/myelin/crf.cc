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

Flow::Variable *CRF::Build(Flow *flow, Flow::Variable *input, bool learn) {
  // Build CRF cell.
  FlowBuilder f(flow, name_);
  auto dt = input->type;
  int num_labels = input->dim(1) + 2;

  // Build inputs.
  auto *x = f.Placeholder("input", dt, input->shape, true);
  auto *alpha_in = f.Placeholder("alpha_in", dt, {1, num_labels}, true);
  flow->Connect({input, x});

  // Add BOS and EOS labels to input emissions.
  auto *zeroes = f.Const(nullptr, dt, {1, 2});
  auto *emissions = f.Name(f.Concat({x, zeroes}, 1), "emissions");

  // Transition weights.
  auto *transitions = f.Parameter("transitions", dt, {num_labels, num_labels});

  // Compute scores.
  auto *scores = f.Add(f.Add(emissions, alpha_in), transitions);

  // Compute alpha_out.
  auto *alpha_out = f.Name(f.LogSumExp(scores, 0, true), "alpha_out");
  alpha_out->set_out()->set_ref();

  CHECK(alpha_out);

  // Build gradients for learning.
  Flow::Variable *dinput = nullptr;
  if (learn) {
    Gradient(flow, f.func());
    dinput = flow->GradientVar(x);
    CHECK(dinput != nullptr);
    flow->Connect({dinput, input});
  }

  return dinput;
}

void CRF::Initialize(const Network &net) {
}

}  // namespace myelin
}  // namespace sling


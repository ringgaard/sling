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

#include <math.h>

#include "sling/myelin/builder.h"

namespace sling {
namespace myelin {

CRF::Variables CRF::Build(Flow *flow,
                          Flow::Variable *input,
                          Flow::Variable *dinput) {
  Variables vars;
  auto dt = input->type;
  int num_labels = input->dim(1);
  bool learn = dinput != nullptr;

  // Transition weights (prev, curr).
  auto *transitions =
      flow->AddVariable(name_ + "/transitions", dt, {num_labels, num_labels});
  if (learn) {
    transitions->set_learnable();
    transitions->init = Flow::Variable::INIT_UNIFORM;
  }

  if (learn) {
    // Build forward function.
    FlowBuilder f(flow, name_ + "/forward");
    vars.input = f.Placeholder("input", dt, input->shape, true);
    vars.alpha_in = f.Placeholder("alpha_in", dt, {1, num_labels}, true);
    vars.input->set_out();
    vars.prev = f.Placeholder("prev", DT_INT32, {1});
    vars.curr = f.Placeholder("curr", DT_INT32, {1});

    // Compute potentials.
    auto *potentials = f.Name(f.Add(vars.input, transitions), "potentials");
    potentials->set_out();

    // Compute scores.
    auto *scores = f.Add(potentials, vars.alpha_in);

    // Compute alpha_out.
    vars.alpha_out = f.ExpandDims(f.LogSumExp(scores, 1), 0);
    f.Name(vars.alpha_out , "alpha_out");
    vars.alpha_out->set_out()->set_ref();

    // Compute score for element.
    std::vector<int> first{0};
    auto *e_index = f.Concat({f.Const(first), vars.curr});
    auto *e_score = f.Gather(vars.input, e_index);

    auto *prev_curr = f.Concat({vars.prev, vars.curr})->set_out();
    auto *t_score = f.Gather(transitions, prev_curr);

    vars.score = f.Name(f.Add(t_score, e_score), "score");
    vars.score->set_out();

    // Build likelihood function.
    FlowBuilder l(flow, name_ + "/likelihood");
    auto *score = l.Placeholder("score", dt, {});
    auto *alpha = l.Placeholder("alpha", dt, {1, num_labels}, true);
    auto *logz = l.Name(l.LogSumExp(alpha), "logz")->set_out();
    l.Name(l.Sub(logz, score), "nll")->set_out();

    // Build backward function.
    FlowBuilder b(flow, name_ + "/backward");
    auto *primal = b.Name(b.Instance(b.func()), "primal");
    auto *b_logz = b.Placeholder("logz", dt, {});
    vars.beta_in = b.Placeholder("beta_in", dt, {1, num_labels}, true);

    auto *b_potentials = b.Name(b.Ref(primal, potentials), "potentials");
    auto *b_alpha_in = b.Name(b.Ref(primal, vars.alpha_in), "alpha_in");
    auto *b_curr = b.Name(b.Ref(primal, vars.curr), "curr");
    auto *b_prev_curr = b.Name(b.Ref(primal, prev_curr), "prev_curr");

    auto *beta = b.LogSumExp(b.Add(b_potentials, vars.beta_in), 1);
    vars.beta_out = b.Name(b.ExpandDims(beta, 0), "beta_out");
    vars.beta_out->set_ref()->set_out();

    auto *outer = b.Add(b.Reshape(b_alpha_in, {num_labels, 1}), vars.beta_out);
    auto *p = b.Exp(b.Sub(b.Add(outer, b_potentials), b_logz));
    b.Name(p, "p");

    // Compute gradients for transitions and input emissions.
    auto *d_transitions = b.Var("d_transitions", dt, {num_labels, num_labels});
    transitions->SetAttr("gradient", d_transitions->name);

    b.AssignAdd(d_transitions, p);
    b.AssignAddScatter(d_transitions, b_prev_curr, b.Const(-1.0f));

    vars.dinput = b.Sub(b.Sum(p, 0, true), b.OneHot(b_curr, num_labels));
    b.Name(vars.dinput, "dinput")->set_out()->set_ref();

    // Connect learning variables.
    flow->Connect({input, vars.input});
    flow->Connect({vars.alpha_out, vars.alpha_in, alpha});
    flow->Connect({vars.beta_out, vars.beta_in});
    flow->Connect({transitions, d_transitions});
    flow->Connect({dinput, vars.dinput, input});
  }

  // Build viterbi decoding function.
  FlowBuilder v(flow, name_ + "/viterbi");
  auto *v_input = v.Placeholder("input", dt, input->shape, true);
  auto *v_alpha_in = v.Placeholder("alpha_in", dt, {1, num_labels}, true);

  // Compute potentials.
  auto *v_potentials = v.Name(v.Add(v_input, transitions), "potentials");

  // Compute max-marginals.
  auto *v_scores = v.Add(v_potentials, v_alpha_in);
  Flow::Variable *max;
  auto *bp = v.Name(v.ArgMax(v_scores, 0, &max), "bp");
  bp->set_out()->set_ref();
  auto *v_alpha_out = v.Name(v.Add(v_alpha_in, max), "alpha_out");
  v_alpha_out->set_out()->set_ref();

  // Connect variables.
  flow->Connect({v_input, input});
  flow->Connect({v_alpha_in, v_alpha_out});

  return vars;
}

void CRF::Initialize(const Network &net) {
  forward_ = net.GetCell(name_ + "/forward");
  if (forward_ != nullptr) {
    forward_input_ = net.GetParameter(name_ + "/forward/input");
    forward_prev_ = net.GetParameter(name_ + "/forward/prev");
    forward_curr_ = net.GetParameter(name_ + "/forward/curr");
    forward_alpha_in_ = net.GetParameter(name_ + "/forward/alpha_in");
    forward_alpha_out_ = net.GetParameter(name_ + "/forward/alpha_out");
    forward_score_ = net.GetParameter(name_ + "/forward/score");
  }

  backward_ = net.GetCell(name_ + "/backward");
  if (backward_ != nullptr) {
    backward_primal_ = net.GetParameter(name_ + "/backward/primal");
    backward_logz_ = net.GetParameter(name_ + "/backward/logz");
    backward_beta_in_ = net.GetParameter(name_ + "/backward/beta_in");
    backward_beta_out_ = net.GetParameter(name_ + "/backward/beta_out");
    backward_dinput_ = net.GetParameter(name_ + "/backward/dinput");
  }

  likelihood_ = net.GetCell(name_ + "/likelihood");
  if (likelihood_ != nullptr) {
    likelihood_alpha_ = net.GetParameter(name_ + "/likelihood/alpha");
    likelihood_score_ = net.GetParameter(name_ + "/likelihood/score");
    likelihood_logz_ = net.GetParameter(name_ + "/likelihood/logz");
    likelihood_nll_ = net.GetParameter(name_ + "/likelihood/nll");
  }

  viterbi_ = net.GetCell(name_ + "/viterbi");
  if (viterbi_ != nullptr) {
    viterbi_input_ = net.GetParameter(name_ + "/viterbi/input");
    viterbi_alpha_in_ = net.GetParameter(name_ + "/viterbi/alpha_in");
    viterbi_alpha_out_ = net.GetParameter(name_ + "/viterbi/alpha_out");
    viterbi_bp_ = net.GetParameter(name_ + "/viterbi/bp");
  }

  num_labels_ = forward_input_->elements();
}

void CRF::Predictor::Predict(Channel *input, std::vector<int> *labels) {
  // Get input sequence length and allocate channels.
  int length = input->size();
  bp_.resize(length);
  alpha_.resize(2);

  // Compute max-marginals and backtrace pointers.
  alpha_.zero(0);
  for (int t = 0; t < length; ++t) {
    viterbi_.Set(crf_->viterbi_input_, input, t);
    viterbi_.Set(crf_->viterbi_alpha_in_, &alpha_, t % 2);
    viterbi_.Set(crf_->viterbi_alpha_out_, &alpha_, (t + 1) % 2);
    viterbi_.Set(crf_->viterbi_bp_, &bp_, t);
    viterbi_.Compute();
  }

  // Find best label for last element.
  float *alpha = viterbi_.Get<float>(crf_->viterbi_alpha_out_);
  int label = -1;
  float score = -INFINITY;
  for (int i = 0; i < crf_->num_labels_; ++i) {
    if (alpha[i] > score) {
      label = i;
      score = alpha[i];
    }
  }

  // Extract the best path by brack-tracking.
  labels->resize(length);
  for (int t = length - 1; t >= 0; t++) {
    (*labels)[t] = label;
    int *bp = bp_.get<int>(t);
    label = bp[label];
  }
}

float CRF::Learner::Learn(Channel *input,
                          const std::vector<int> &labels,
                          Channel *dinput) {
  // Get input size.
  int length = input->size();
  alpha_.resize(length + 1);
  beta_.resize(length + 1);

  // Label 0 is used as the start symbol.
  alpha_.zero(0);
  float *alpha0 = alpha_.get<float>(0);
  alpha0[0] = 1.0;

  // Run forward pass.
  forward_.Resize(length);
  float score = 0.0;
  int prev = 0;
  for (int t = 0; t < length; ++t) {
    Instance &data = forward_[t];
    int curr = labels[t];
    data.Set(crf_->forward_input_, input, t);
    *data.Get<int>(crf_->forward_prev_) = prev;
    *data.Get<int>(crf_->forward_curr_) = curr;
    data.Set(crf_->forward_alpha_in_, &alpha_, t);
    data.Set(crf_->forward_alpha_out_, &alpha_, t + 1);
    data.Compute();
    score += *data.Get<float>(crf_->forward_score_);
    prev = curr;
  }

  // Compute partition function and loss (negative log-likelihood).
  likelihood_.Set(crf_->likelihood_alpha_, &alpha_, length);
  *likelihood_.Get<float>(crf_->likelihood_score_) = score;
  likelihood_.Compute();
  float logz = *likelihood_.Get<float>(crf_->likelihood_logz_);
  float nll = *likelihood_.Get<float>(crf_->likelihood_nll_);

  // Run backward pass.
  beta_.zero(length);
  for (int t = length - 1; t >= 0; --t) {
    backward_.Set(crf_->backward_primal_, &forward_[t]);
    *backward_.Get<float>(crf_->backward_logz_) = logz;
    backward_.Set(crf_->backward_beta_in_, &beta_, t + 1);
    backward_.Set(crf_->backward_beta_out_, &beta_, t);
    backward_.Set(crf_->backward_dinput_, dinput, t);
    backward_.Compute();
  }

  // Return loss.
  return nll;
}

void CRF::Learner::CollectGradients(Instances *gradients) {
  gradients->Add(&backward_);
}

}  // namespace myelin
}  // namespace sling


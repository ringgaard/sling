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

void CRF::Build(Flow *flow, Flow::Variable *input, Flow::Variable *dinput) {
  auto dt = input->type;
  int num_labels = input->dim(1);
  bool learn = dinput != nullptr;

  // Transition weights (prev, curr).
  auto *transitions = flow->AddVariable(name_ + "/transitions",
                                        dt, {num_labels, num_labels});
  transitions->set_learnable();

  if (learn) {
    // Build forward function.
    FlowBuilder f(flow, name_ + "/forward");
    auto *f_input = f.Placeholder("input", dt, input->shape, true);
    auto *f_alpha_in = f.Placeholder("alpha_in", dt, {1, num_labels}, true);
    auto *f_prev = f.Placeholder("prev", DT_INT32, {1});
    auto *f_curr = f.Placeholder("curr", DT_INT32, {1});

    // Compute forward potentials.
    auto *f_potentials = f.Add(f_input, transitions);

    // Compute alpha.
    auto *f_scores = f.Add(f_potentials, f.ReverseDims(f_alpha_in));
    auto *f_alpha_out = f.ExpandDims(f.LogSumExp(f_scores, 0), 0);
    f.Name(f_alpha_out, "alpha_out")->set_out()->set_ref();

    // Compute score for element.
    std::vector<int> first{0};
    auto *e_index = f.Concat({f.Const(first), f_curr});
    auto *e_score = f.Gather(f_input, e_index);

    auto *prev_curr = f.Concat({f_prev, f_curr});
    auto *t_score = f.Gather(transitions, prev_curr);

    auto *f_score = f.Add(t_score, e_score);
    f.Name(f_score, "score")->set_out();

    // Build backward function.
    FlowBuilder b(flow, name_ + "/backward");
    auto *b_input = b.Placeholder("input", dt, input->shape, true);
    auto *b_beta_in = b.Placeholder("beta_in", dt, {1, num_labels}, true);

    // Compute backward potentials.
    auto *b_potentials = b.Add(b_input, transitions);

    // Compute beta.
    auto *b_scores = b.Add(b_potentials, b_beta_in);
    auto *b_beta_out = b.ExpandDims(b.LogSumExp(b_scores, 1), 0);
    b.Name(b_beta_out, "beta_out")->set_ref()->set_out();

    // Build likelihood function.
    FlowBuilder l(flow, name_ + "/likelihood");
    auto *l_score = l.Placeholder("score", dt, {});
    auto *l_alpha = l.Placeholder("alpha", dt, {1, num_labels}, true);
    auto *l_logz = l.Name(l.LogSumExp(l_alpha), "logz")->set_out();
    l.Name(l.Sub(l_logz, l_score), "nll")->set_out();

    // Build gradient function.
    FlowBuilder g(flow, name_ + "/gradient");
    auto *g_input = g.Placeholder("input", dt, input->shape, true);
    auto *g_prev = g.Placeholder("prev", DT_INT32, {1});
    auto *g_curr = g.Placeholder("curr", DT_INT32, {1});
    auto *g_alpha = g.Placeholder("alpha", dt, {1, num_labels}, true);
    auto *g_beta = g.Placeholder("beta", dt, {1, num_labels}, true);
    auto *g_logz = g.Placeholder("logz", dt, {});

    auto *g_potentials = g.Add(g_input, transitions);
    auto *outer = g.Add(g.ReverseDims(g_alpha), g_beta);
    auto *p = g.Exp(g.Sub(g.Add(outer, g_potentials), g_logz));
    g.Name(p, "p");

    // Compute gradient for transitions.
    auto *d_transitions = g.Var("d_transitions", dt, {num_labels, num_labels});
    transitions->SetAttr("gradient", d_transitions->name);
    g.AssignAdd(d_transitions, p);
    auto *g_prev_curr = g.Concat({g_prev, g_curr});
    g.AssignAddScatter(d_transitions, g_prev_curr, g.Const(-1.0f));

    // Compute gradient for emissions.
    auto d_input = g.Sub(g.Sum(p, 0, true), g.OneHot(g_curr, num_labels));
    g.Name(d_input, "d_input")->set_out()->set_ref();

    // Build gradient function for token 0 (special case).
    FlowBuilder g0(flow, name_ + "/gradient0");
    auto *g0_input = g0.Placeholder("input", dt, input->shape, true);
    auto *g0_curr = g0.Placeholder("curr", DT_INT32, {1});
    auto *g0_beta = g0.Placeholder("beta", dt, {1, num_labels}, true);
    auto *g0_logz = g0.Placeholder("logz", dt, {});
    auto *p0 = g0.Exp(g0.Sub(g0.Add(g0_input, g0_beta), g0_logz));
    auto d0_input = g0.Sub(p0, g0.OneHot(g0_curr, num_labels));
    g0.Name(d0_input, "d_input")->set_out()->set_ref();

    // Connect learning variables.
    flow->Connect({input, f_input, b_input, g_input, g0_input});
    flow->Connect({f_alpha_in, f_alpha_out, g_alpha, l_alpha, input});
    flow->Connect({b_beta_in, b_beta_out, g_beta, g0_beta});
    flow->Connect({transitions, d_transitions});
    flow->Connect({dinput, input, d_input});
  }

  // Build viterbi decoding function.
  FlowBuilder v(flow, name_ + "/viterbi");
  auto *v_input = v.Placeholder("input", dt, input->shape, true);
  auto *v_alpha_in = v.Placeholder("alpha_in", dt, {1, num_labels}, true);

  // Compute potentials.
  auto *v_potentials = v.Name(v.Add(v_input, transitions), "potentials");

  // Compute max-marginals.
  auto *v_scores = v.Add(v_potentials, v.ReverseDims(v_alpha_in));
  Flow::Variable *max;
  auto *bp = v.Name(v.ExpandDims(v.ArgMax(v_scores, 0, &max), 0), "bp");
  bp->set_out()->set_ref();
  auto *v_alpha_out = v.Name(v.ExpandDims(max, 0), "alpha_out");
  v_alpha_out->set_out()->set_ref();

  // Connect variables.
  flow->Connect({v_input, input});
  flow->Connect({v_alpha_in, v_alpha_out, input});
}

void CRF::Initialize(const Network &net) {
  forward_ = net.LookupCell(name_ + "/forward");
  if (forward_ != nullptr) {
    forward_input_ = net.GetParameter(name_ + "/forward/input");
    forward_prev_ = net.GetParameter(name_ + "/forward/prev");
    forward_curr_ = net.GetParameter(name_ + "/forward/curr");
    forward_alpha_in_ = net.GetParameter(name_ + "/forward/alpha_in");
    forward_alpha_out_ = net.GetParameter(name_ + "/forward/alpha_out");
    forward_score_ = net.GetParameter(name_ + "/forward/score");
  }

  backward_ = net.LookupCell(name_ + "/backward");
  if (backward_ != nullptr) {
    backward_input_ = net.GetParameter(name_ + "/backward/input");
    backward_beta_in_ = net.GetParameter(name_ + "/backward/beta_in");
    backward_beta_out_ = net.GetParameter(name_ + "/backward/beta_out");
  }

  likelihood_ = net.LookupCell(name_ + "/likelihood");
  if (likelihood_ != nullptr) {
    likelihood_alpha_ = net.GetParameter(name_ + "/likelihood/alpha");
    likelihood_score_ = net.GetParameter(name_ + "/likelihood/score");
    likelihood_logz_ = net.GetParameter(name_ + "/likelihood/logz");
    likelihood_nll_ = net.GetParameter(name_ + "/likelihood/nll");
  }

  gradient_ = net.LookupCell(name_ + "/gradient");
  if (gradient_ != nullptr) {
    gradient_input_ = net.GetParameter(name_ + "/gradient/input");
    gradient_prev_ = net.GetParameter(name_ + "/gradient/prev");
    gradient_curr_ = net.GetParameter(name_ + "/gradient/curr");
    gradient_logz_ = net.GetParameter(name_ + "/gradient/logz");
    gradient_alpha_ = net.GetParameter(name_ + "/gradient/alpha");
    gradient_beta_ = net.GetParameter(name_ + "/gradient/beta");
    gradient_dinput_ = net.GetParameter(name_ + "/gradient/d_input");
  }

  gradient0_ = net.LookupCell(name_ + "/gradient0");
  if (gradient0_ != nullptr) {
    gradient0_input_ = net.GetParameter(name_ + "/gradient0/input");
    gradient0_curr_ = net.GetParameter(name_ + "/gradient0/curr");
    gradient0_logz_ = net.GetParameter(name_ + "/gradient0/logz");
    gradient0_beta_ = net.GetParameter(name_ + "/gradient0/beta");
    gradient0_dinput_ = net.GetParameter(name_ + "/gradient0/d_input");
  }

  viterbi_ = net.GetCell(name_ + "/viterbi");
  if (viterbi_ != nullptr) {
    viterbi_input_ = net.GetParameter(name_ + "/viterbi/input");
    viterbi_alpha_in_ = net.GetParameter(name_ + "/viterbi/alpha_in");
    viterbi_alpha_out_ = net.GetParameter(name_ + "/viterbi/alpha_out");
    viterbi_bp_ = net.GetParameter(name_ + "/viterbi/bp");
  }

  num_labels_ = viterbi_input_->elements();
}

void CRF::Predictor::Predict(Channel *input, std::vector<int> *labels) {
  // Get input sequence length and allocate channels.
  int length = input->size();
  bp_.resize(length);
  alpha_.resize(2);

  // Compute max-marginals and backtrace pointers.
  alpha_.set(0, input->at(0));
  for (int t = 1; t < length; ++t) {
    viterbi_.Set(crf_->viterbi_input_, input, t);
    viterbi_.Set(crf_->viterbi_alpha_in_, &alpha_, (t + 1) % 2);
    viterbi_.Set(crf_->viterbi_alpha_out_, &alpha_, t % 2);
    viterbi_.Set(crf_->viterbi_bp_, &bp_, t);
    viterbi_.Compute();
  }

  // Find best label for last element.
  float *alpha = viterbi_.GetRef<float>(crf_->viterbi_alpha_out_);
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
  for (int t = length - 1; t >= 0; --t) {
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
  alpha_.resize(length);
  beta_.resize(length);

  // Compute alpha and score for first token.
  alpha_.set(0, input->at(0));
  float score = input->get<float>(0)[labels[0]];

  // Run forward pass to compute alpha for remaining tokens.
  int prev = labels[0];
  for (int t = 1; t < length; ++t) {
    int curr = labels[t];
    forward_.Set(crf_->forward_input_, input, t);
    *forward_.Get<int>(crf_->forward_prev_) = prev;
    *forward_.Get<int>(crf_->forward_curr_) = curr;
    forward_.Set(crf_->forward_alpha_in_, &alpha_, t - 1);
    forward_.Set(crf_->forward_alpha_out_, &alpha_, t);
    forward_.Compute();
    score += *forward_.Get<float>(crf_->forward_score_);
    prev = curr;
  }

  // Run backward pass to compute beta.
  beta_.zero(length - 1);
  for (int t = length - 1; t > 0; --t) {
    backward_.Set(crf_->backward_input_, input, t);
    backward_.Set(crf_->backward_beta_in_, &beta_, t);
    backward_.Set(crf_->backward_beta_out_, &beta_, t - 1);
    backward_.Compute();
  }

  // Compute partition function and loss (negative log-likelihood).
  likelihood_.Set(crf_->likelihood_alpha_, &alpha_, length - 1);
  *likelihood_.Get<float>(crf_->likelihood_score_) = score;
  likelihood_.Compute();
  float logz = *likelihood_.Get<float>(crf_->likelihood_logz_);
  float nll = *likelihood_.Get<float>(crf_->likelihood_nll_);

  // Compute gradients for first token.
  gradient0_.Set(crf_->gradient0_input_, input, 0);
  *gradient0_.Get<int>(crf_->gradient0_curr_) = labels[0];
  *gradient0_.Get<float>(crf_->gradient0_logz_) = logz;
  gradient0_.Set(crf_->gradient0_beta_, &beta_, 0);
  gradient0_.Set(crf_->gradient0_dinput_, dinput, 0);
  gradient0_.Compute();

  // Compute gradients for remaining tokens.
  prev = labels[0];
  for (int t = 1; t < length; ++t) {
    int curr = labels[t];
    gradient_.Set(crf_->gradient_input_, input, t);
    *gradient_.Get<int>(crf_->gradient_prev_) = prev;
    *gradient_.Get<int>(crf_->gradient_curr_) = curr;
    *gradient_.Get<float>(crf_->gradient_logz_) = logz;
    gradient_.Set(crf_->gradient_alpha_, &alpha_, t - 1);
    gradient_.Set(crf_->gradient_beta_, &beta_, t);
    gradient_.Set(crf_->gradient_dinput_, dinput, t);
    gradient_.Compute();
    prev = curr;
  }

  // Return loss.
  return nll;
}

void CRF::Learner::CollectGradients(Instances *gradients) {
  gradients->Add(&gradient_);
}

}  // namespace myelin
}  // namespace sling


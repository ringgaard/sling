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

  // Create zero input tensor.
  auto *zero = flow->AddConstant(name_ + "/zero", input->type, input->shape);
  zero->set_out();
  flow->Connect({input, zero});

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
    auto *beta = flow->GradientVar(alpha);
    flow->Connect({dinput, vars.dinput});
    flow->Connect({vars.beta_out, vars.beta_in, beta});
  }

  // Build loss function.
  loss_.Build(flow);

  return vars;
}

void CRF::Initialize(const Network &net) {
  step_ = net.GetCell(name_ + "/step");
  step_input_ = net.GetParameter(name_ + "/step/input");
  step_prev_ = net.GetParameter(name_ + "/step/prev");
  step_curr_ = net.GetParameter(name_ + "/step/curr");
  step_alpha_in_ = net.GetParameter(name_ + "/step/alpha_in");
  step_alpha_out_ = net.GetParameter(name_ + "/step/alpha_out");
  step_score_ = net.GetParameter(name_ + "/step/score");
  zero_ = net.GetParameter(name_ + "/zero");

  gstep_ = step_->Gradient();
  if (gstep_ != nullptr) {
    gstep_primal_ = step_->Primal();
    gstep_dscore_ = step_score_->Gradient();
    gstep_beta_in_ = step_alpha_out_->Gradient();
    gstep_beta_out_ = step_alpha_in_->Gradient();
    gstep_dinput_ = step_input_->Gradient();

    likelihood_ = net.GetCell(name_ + "/likelihood");
    likelihood_alpha_ = net.GetParameter(name_ + "/likelihood/alpha");
    likelihood_score_ = net.GetParameter(name_ + "/likelihood/score");
    likelihood_p_ = net.GetParameter(name_ + "/likelihood/p");

    glikelihood_ = likelihood_->Gradient();
    glikelihood_primal_ = likelihood_->Primal();
    glikelihood_dscore_ = likelihood_score_->Gradient();
    glikelihood_beta_ = likelihood_alpha_->Gradient();

    loss_.Initialize(net);
  }

  // Get label indices for BOS and EOS.
  num_labels_ = step_input_->elements();
  bos_ = num_labels_;
  eos_ = num_labels_ + 1;
}

float CRF::Learner::Learn(Channel *input,
                          const std::vector<int> &labels,
                          Channel *dinput) {
  // Get input size.
  int length = input->size();
  alpha_.resize(length + 2);
  beta_.resize(length + 2);

  // Set up initial alpha where only BOS is allowed.
  float *alpha0 = alpha_.get<float>(0);
  for (int i = 0; i < length + 2; ++i) {
    alpha0[i] = i == crf_->bos_ ? 0 : CRF::IMPOSSIBLE;
  }

  // Run forward pass.
  float score = 0.0;
  forward_.Resize(length + 1);
  int prev = crf_->bos_;
  for (int i = 0; i < length; ++i) {
    Instance &data = forward_[i];
    int curr = labels[i];
    data.Set(crf_->step_input_, input, i);
    *data.Get<int>(crf_->step_prev_) = prev;
    *data.Get<int>(crf_->step_curr_) = curr;
    data.Set(crf_->step_alpha_in_, &alpha_, i);
    data.Set(crf_->step_alpha_out_, &alpha_, i + 1);
    data.Compute();
    //LOG(INFO) << "crf fwd" << i << ":\n" << data.ToString();

    score += *data.Get<float>(crf_->step_score_);
    prev = curr;
  }

  // Run last forward step for EOS.
  Instance &data = forward_[length];
  data.SetReference(crf_->step_input_, crf_->zero_->data());
  *data.Get<int>(crf_->step_prev_) = prev;
  *data.Get<int>(crf_->step_curr_) = crf_->eos_;
  data.Set(crf_->step_alpha_in_, &alpha_, length);
  data.Set(crf_->step_alpha_out_, &alpha_, length + 1);
  data.Compute();
  //LOG(INFO) << "crf fwd last:\n" << data.ToString();
  score += *data.Get<float>(crf_->step_score_);

  // Compute likelihood.
  likelihood_.Set(crf_->likelihood_alpha_, &alpha_, length + 1);
  *likelihood_.Get<float>(crf_->likelihood_score_) = score;
  likelihood_.Compute();
  float p = *likelihood_.Get<float>(crf_->likelihood_p_);

  // Compute loss.
  float dp;
  float loss = crf_->loss_.Compute(p, &dp);
  LOG(INFO) << "score=" << score << " p=" << p << " dp=" << dp << " loss=" << loss;

  // Compute likelihood gradient.
  glikelihood_.Set(crf_->glikelihood_primal_, &likelihood_);
  *glikelihood_.Get<float>(crf_->glikelihood_dscore_) = dp;
  glikelihood_.Set(crf_->glikelihood_beta_, &beta_, length + 1);
  glikelihood_.Compute();

  // Run first gradient step for EOS.
  backward_.Set(crf_->gstep_primal_, &forward_[length]);
  *backward_.Get<float>(crf_->gstep_dscore_) = dp;
  backward_.Set(crf_->gstep_beta_in_, &beta_, length + 1);
  backward_.Set(crf_->gstep_beta_out_, &beta_, length);
  backward_.Set(crf_->gstep_dinput_, dinput, 0);
  backward_.Compute();

  // Run backward pass.
  for (int i = length - 1; i >= 0; --i) {
    backward_.Set(crf_->gstep_primal_, &forward_[i]);
    *backward_.Get<float>(crf_->gstep_dscore_) = dp;
    backward_.Set(crf_->gstep_beta_in_, &beta_, i + 1);
    backward_.Set(crf_->gstep_beta_out_, &beta_, i);
    backward_.Set(crf_->gstep_dinput_, dinput, i);
    backward_.Compute();
  }

  // Return loss.
  return loss;
}

void CRF::Learner::CollectGradients(Instances *gradients) {
  gradients->Add(&backward_);
}

}  // namespace myelin
}  // namespace sling


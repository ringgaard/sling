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
  int num_labels = input->dim(1) + 2;
  //bool learn = dinput != nullptr;

  // Build forward function.
  FlowBuilder f(flow, name_ + "/forward");

  // Build inputs.
  vars.input = f.Placeholder("input", dt, input->shape, true);
  vars.alpha_in = f.Placeholder("alpha_in", dt, {1, num_labels}, true);
  vars.input->set_out();
  vars.prev = f.Placeholder("prev", DT_INT32, {1});
  vars.curr = f.Placeholder("curr", DT_INT32, {1});

  // Add BOS and EOS labels to input emissions.
  auto *padding = f.Const(nullptr, dt, {1, 2});
  auto *emissions = f.Name(f.Concat({vars.input, padding}, 1), "emissions");
  //f.RandomUniform(emissions);

  // Transition weights (prev, curr).
  auto *transitions = f.Parameter("transitions", dt, {num_labels, num_labels});

  // Compute potentials.
  auto *potentials = f.Name(f.Add(emissions, transitions), "potentials");
  potentials->set_out();

  // Compute scores.
  auto *scores = f.Add(potentials, vars.alpha_in);

  // Compute alpha_out.
  vars.alpha_out = f.Reshape(f.LogSumExp(scores, 1), {1, num_labels});
  f.Name(vars.alpha_out , "alpha_out");
  vars.alpha_out->set_out()->set_ref();

  // Compute score for element.
  std::vector<int> first{0};
  auto *e_index = f.Concat({f.Const(first), vars.curr});
  auto *e_score = f.Gather(emissions, e_index);

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
  auto *b_prev_curr = b.Name(b.Ref(primal, prev_curr), "prev_curr");

  auto *beta = b.LogSumExp(b.Add(b_potentials, vars.beta_in), 1);
  vars.beta_out = b.Name(b.Reshape(beta, {1, num_labels}), "beta_out");
  vars.beta_out->set_ref()->set_out();

  auto *outer = b.Add(b.Reshape(b_alpha_in, {num_labels, 1}), vars.beta_out);
  auto *p = b.Exp(b.Sub(b.Add(outer, b_potentials), b_logz));
  //p = b.Clip(p, 0.0, 1.0);
  p->set_out();
  b.Name(p, "p");

  auto *d_transitions = b.Var("d_transitions", dt, {num_labels, num_labels});
  transitions->SetAttr("gradient", d_transitions->name);

  b.AssignAdd(d_transitions, p);
  b.AssignAddScatter(d_transitions, b_prev_curr, b.Const(-1.0f));

  // Create zero input tensor.
  auto *zero = flow->AddConstant(name_ + "/zero", input->type, input->shape);
  zero->set_out();
  flow->Connect({input, zero});

  // Connect variables.
  flow->Connect({input, vars.input});
  flow->Connect({vars.alpha_out, vars.alpha_in, alpha});
  flow->Connect({vars.beta_out, vars.beta_in});
  flow->Connect({transitions, d_transitions});

  return vars;
}

void CRF::Initialize(const Network &net) {
  forward_ = net.GetCell(name_ + "/forward");
  forward_input_ = net.GetParameter(name_ + "/forward/input");
  forward_prev_ = net.GetParameter(name_ + "/forward/prev");
  forward_curr_ = net.GetParameter(name_ + "/forward/curr");
  forward_alpha_in_ = net.GetParameter(name_ + "/forward/alpha_in");
  forward_alpha_out_ = net.GetParameter(name_ + "/forward/alpha_out");
  forward_score_ = net.GetParameter(name_ + "/forward/score");
  zero_ = net.GetParameter(name_ + "/zero");

  backward_ = net.GetCell(name_ + "/backward");
  if (backward_ != nullptr) {
    backward_primal_ = net.GetParameter(name_ + "/backward/primal");
    backward_logz_ = net.GetParameter(name_ + "/backward/logz");
    backward_beta_in_ = net.GetParameter(name_ + "/backward/beta_in");
    backward_beta_out_ = net.GetParameter(name_ + "/backward/beta_out");

    backward_p_ = net.GetParameter(name_ + "/backward/p");

    likelihood_ = net.GetCell(name_ + "/likelihood");
    likelihood_alpha_ = net.GetParameter(name_ + "/likelihood/alpha");
    likelihood_score_ = net.GetParameter(name_ + "/likelihood/score");
    likelihood_logz_ = net.GetParameter(name_ + "/likelihood/logz");
    likelihood_nll_ = net.GetParameter(name_ + "/likelihood/nll");
  }

  // Get label indices for BOS and EOS.
  num_labels_ = forward_input_->elements() + 2;
  bos_ = num_labels_ - 2;
  eos_ = num_labels_ - 1;
}

float CRF::Learner::Learn(Channel *input,
                          const std::vector<int> &labels,
                          Channel *dinput) {
  // Get input size.
  int length = input->size();
  alpha_.resize(length + 2);
  beta_.resize(length + 2);

  // Set up initial alpha where only BOS is allowed.
  alpha_.zero(0);
  float *alpha0 = alpha_.get<float>(0);
  alpha0[crf_->bos_] = 1.0;

#if 0
  for (int i = 0; i < crf_->num_labels_; ++i) {
    //alpha0[i] = i == crf_->bos_ ? 0.0 : CRF::IMPOSSIBLE;
    alpha0[i] = i == crf_->bos_ ? 1.0 : 0.0;
  }
#endif

  // Run forward pass.
  float score = 0.0;
  forward_.Resize(length + 1);
  int prev = crf_->bos_;
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

    //LOG(INFO) << "crf fwd " << t << ":\n" << data.ToString();
    LOG(INFO) << "crf fwd" << t << " score=" << (*data.Get<float>(crf_->forward_score_)); // << ":\n" << data.ToString();
    //LOG(INFO) << "crf fwd input " << t << ":\n" << data.ToString(crf_->forward_input_);
  }

  // Run last forward step for EOS.
  Instance &data = forward_[length];
  data.SetReference(crf_->forward_input_, crf_->zero_->data());
  *data.Get<int>(crf_->forward_prev_) = prev;
  *data.Get<int>(crf_->forward_curr_) = crf_->eos_;
  data.Set(crf_->forward_alpha_in_, &alpha_, length);
  data.Set(crf_->forward_alpha_out_, &alpha_, length + 1);
  data.Compute();
  score += *data.Get<float>(crf_->forward_score_);

  //LOG(INFO) << "crf fwd last:\n" << data.ToString();

  // Compute partition function and loss (negative log-likelihood).
  likelihood_.Set(crf_->likelihood_alpha_, &alpha_, length + 1);
  *likelihood_.Get<float>(crf_->likelihood_score_) = score;
  likelihood_.Compute();
  float logz = *likelihood_.Get<float>(crf_->likelihood_logz_);
  float nll = *likelihood_.Get<float>(crf_->likelihood_nll_);

  LOG(INFO) << "score=" << score << " nll=" << nll << " p=" << exp(-nll) << " logz=" << logz;

  // Run first backward step for EOS.
  beta_.zero(length + 1);
  backward_.Set(crf_->backward_primal_, &forward_[length]);
  *backward_.Get<float>(crf_->backward_logz_) = logz;
  backward_.Set(crf_->backward_beta_in_, &beta_, length + 1);
  backward_.Set(crf_->backward_beta_out_, &beta_, length);
  backward_.Compute();

  // Run backward pass.
  for (int t = length - 1; t >= 0; --t) {
    backward_.Set(crf_->backward_primal_, &forward_[t]);
    *backward_.Get<float>(crf_->backward_logz_) = logz;
    backward_.Set(crf_->backward_beta_in_, &beta_, t + 1);
    backward_.Set(crf_->backward_beta_out_, &beta_, t);
    backward_.Compute();

    //LOG(INFO) << "crf bkw " << t << ":\n" << beta_.ToString(i);
    LOG(INFO) << "crf p bkw " << t << ":\n" << backward_.ToString(crf_->backward_p_);

    auto p = backward_[crf_->backward_p_];
    for (int i = 0; i < crf_->num_labels_; ++i) {
      float sum = 0.0;
      for (int j = 0; j < crf_->num_labels_; ++j) {
        sum += p.at<float>(j, i);
      }
      LOG(INFO) << "sum(" << i << ")=" << sum;
    }

    dinput->zero(t);
  }

  //auto *d_transition = crf_->backward_->GetParameter("crf/backward/d_transitions");
  //LOG(INFO) << "crf d_transition:\n" << backward_.ToString(d_transition);

  //auto *transitions = crf_->backward_->GetParameter("crf/forward/transitions");
  //LOG(INFO) << "crf transitions:\n" << transitions->ToString();

  LOG(INFO) << "alpha:\n" << alpha_.ToString();
  LOG(INFO) << "beta:\n" << beta_.ToString();

  // Return loss.
  return nll;
}

void CRF::Learner::CollectGradients(Instances *gradients) {
  gradients->Add(&backward_);
}

}  // namespace myelin
}  // namespace sling


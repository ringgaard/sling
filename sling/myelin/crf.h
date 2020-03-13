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

#ifndef SLING_MYELIN_CRF_H_
#define SLING_MYELIN_CRF_H_

#include "sling/myelin/compute.h"
#include "sling/myelin/flow.h"
#include "sling/myelin/learning.h"

namespace sling {
namespace myelin {

// Conditional Random Field (CRF) cell.
class CRF {
 public:
  // Score for impossible label.
  static constexpr float IMPOSSIBLE = 1e-2;

  // Flow input/output variables.
  struct Variables {
    Flow::Variable *input;         // input emissions
    Flow::Variable *dinput;        // emissions gradient
    Flow::Variable *prev;          // previous tag
    Flow::Variable *curr;          // current tag
    Flow::Variable *score;         // score

    Flow::Variable *alpha_in;      // alpha input
    Flow::Variable *alpha_out;     // alpha output
    Flow::Variable *beta_in;       // alpha input gradient
    Flow::Variable *beta_out;      // alpha output gradient
  };

  CRF(const string &name = "crf") : name_(name), loss_(name + "/loss") {}

  // Build flow for CRF.
  Variables Build(Flow *flow,
                  Flow::Variable *input,
                  Flow::Variable *dinput = nullptr);

  // Initialize CRF.
  void Initialize(const Network &net);

  // CRF sequence predictor.
  class Predictor {
   public:
    Predictor(const CRF *crf) : crf_(crf) {}

    // Predict label sequence for input.
    void Predict(Channel *input, std::vector<int> *labels);

   private:
    const CRF *crf_;
  };

  // CRF sequence learner.
  class Learner {
   public:
    Learner(const CRF *crf)
        : crf_(crf),
          forward_(crf->step_),
          backward_(crf->gstep_),
          likelihood_(crf->likelihood_),
          glikelihood_(crf->glikelihood_),
          alpha_(crf->likelihood_alpha_),
          beta_(crf->glikelihood_beta_) {}

    // Learn label sequence for input. Returns loss and input gradient.
    float Learn(Channel *input,
                const std::vector<int> &labels,
                Channel *dinput);

    // Collect instances with gradient updates.
    void CollectGradients(Instances *gradients);

   private:
    const CRF *crf_;

    InstanceArray forward_;
    Instance backward_;
    Instance likelihood_;
    Instance glikelihood_;
    Channel alpha_;
    Channel beta_;
  };

 private:
  // CRF cell name.
  string name_;

  // Number of labels (excluding BOS and EOS).
  int num_labels_ = 0;

  // Indices for begin (BOS) and end (EOS) labels.
  int bos_;
  int eos_;

  // CRF step cell.
  Cell *step_ = nullptr;
  Tensor *step_input_ = nullptr;
  Tensor *step_prev_ = nullptr;
  Tensor *step_curr_ = nullptr;
  Tensor *step_alpha_in_ = nullptr;
  Tensor *step_alpha_out_ = nullptr;
  Tensor *step_score_ = nullptr;
  Tensor *zero_ = nullptr;

  // CRF step gradient cell.
  Cell *gstep_ = nullptr;
  Tensor *gstep_primal_ = nullptr;
  Tensor *gstep_dscore_ = nullptr;
  Tensor *gstep_beta_in_ = nullptr;
  Tensor *gstep_beta_out_ = nullptr;
  Tensor *gstep_dinput_ = nullptr;

  // CRF likelihood cell.
  Cell *likelihood_ = nullptr;
  Tensor *likelihood_alpha_ = nullptr;
  Tensor *likelihood_score_ = nullptr;
  Tensor *likelihood_p_ = nullptr;

  // CRF likelihood gradient cell.
  Cell *glikelihood_ = nullptr;
  Tensor *glikelihood_primal_ = nullptr;
  Tensor *glikelihood_dscore_ = nullptr;
  Tensor *glikelihood_beta_ = nullptr;

  // CRF loss function.
  NLLLoss loss_;
};

}  // namespace myelin
}  // namespace sling

#endif  // SLING_MYELIN_CRF_H_


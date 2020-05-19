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

namespace sling {
namespace myelin {

// Conditional Random Field (CRF) module.
class CRF {
 public:
  CRF(const string &name = "crf") : name_(name) {}

  // Build flow for CRF.
  void Build(Flow *flow,
             Flow::Variable *input,
             Flow::Variable *dinput = nullptr);

  // Initialize CRF.
  void Initialize(const Network &net);

  // CRF sequence predictor.
  class Predictor {
   public:
    Predictor(const CRF *crf)
        : crf_(crf),
          viterbi_(crf->viterbi_),
          alpha_(crf->viterbi_alpha_in_),
          bp_(crf->viterbi_bp_) {}

    // Predict label sequence for input using Viterbi decoding.
    void Predict(Channel *input, std::vector<int> *labels);

   private:
    const CRF *crf_;

    Instance viterbi_;
    Channel alpha_;
    Channel bp_;
  };

  // CRF sequence learner.
  class Learner {
   public:
    Learner(const CRF *crf)
        : crf_(crf),
          forward_(crf->forward_),
          backward_(crf->backward_),
          likelihood_(crf->likelihood_),
          gradient0_(crf->gradient0_),
          gradient_(crf->gradient_),
          alpha_(crf->forward_alpha_in_),
          beta_(crf->backward_beta_in_) {}

    // Learn label sequence for input. Returns loss and input gradient.
    float Learn(Channel *input,
                const std::vector<int> &labels,
                Channel *dinput);

    // Collect instances with gradient updates.
    void CollectGradients(Instances *gradients);

   private:
    const CRF *crf_;

    Instance forward_;
    Instance backward_;
    Instance likelihood_;
    Instance gradient0_;
    Instance gradient_;
    Channel alpha_;
    Channel beta_;
  };

 private:
  // CRF cell name.
  string name_;

  // Number of labels.
  int num_labels_ = 0;

  // CRF forward cell.
  Cell *forward_ = nullptr;
  Tensor *forward_input_ = nullptr;
  Tensor *forward_prev_ = nullptr;
  Tensor *forward_curr_ = nullptr;
  Tensor *forward_alpha_in_ = nullptr;
  Tensor *forward_alpha_out_ = nullptr;
  Tensor *forward_score_ = nullptr;

  // CRF backward cell.
  Cell *backward_ = nullptr;
  Tensor *backward_input_ = nullptr;
  Tensor *backward_beta_in_ = nullptr;
  Tensor *backward_beta_out_ = nullptr;

  // CRF likelihood cell.
  Cell *likelihood_ = nullptr;
  Tensor *likelihood_alpha_ = nullptr;
  Tensor *likelihood_score_ = nullptr;
  Tensor *likelihood_logz_ = nullptr;
  Tensor *likelihood_nll_ = nullptr;

  // CRF gradient cell for first token.
  Cell *gradient0_ = nullptr;
  Tensor *gradient0_input_ = nullptr;
  Tensor *gradient0_curr_ = nullptr;
  Tensor *gradient0_logz_ = nullptr;
  Tensor *gradient0_beta_ = nullptr;
  Tensor *gradient0_dinput_ = nullptr;

  // CRF gradient cell.
  Cell *gradient_ = nullptr;
  Tensor *gradient_input_ = nullptr;
  Tensor *gradient_prev_ = nullptr;
  Tensor *gradient_curr_ = nullptr;
  Tensor *gradient_logz_ = nullptr;
  Tensor *gradient_alpha_ = nullptr;
  Tensor *gradient_beta_ = nullptr;
  Tensor *gradient_dinput_ = nullptr;

  // CRF viterbi cell.
  Cell *viterbi_ = nullptr;
  Tensor *viterbi_input_ = nullptr;
  Tensor *viterbi_alpha_in_ = nullptr;
  Tensor *viterbi_alpha_out_ = nullptr;
  Tensor *viterbi_bp_ = nullptr;
};

}  // namespace myelin
}  // namespace sling

#endif  // SLING_MYELIN_CRF_H_


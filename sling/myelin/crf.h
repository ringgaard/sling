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

// Conditional Random Field (CRF) cell.
class CRF {
 public:
  // Score for impossible label.
  static constexpr float IMPOSSIBLE = -1e3; //-1.0; //-1e3;

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

  CRF(const string &name = "crf") : name_(name) {}

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
          forward_(crf->forward_),
          backward_(crf->backward_),
          likelihood_(crf->likelihood_),
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

    InstanceArray forward_;
    Instance backward_;
    Instance likelihood_;
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

  // CRF forward cell.
  Cell *forward_ = nullptr;
  Tensor *forward_input_ = nullptr;
  Tensor *forward_prev_ = nullptr;
  Tensor *forward_curr_ = nullptr;
  Tensor *forward_alpha_in_ = nullptr;
  Tensor *forward_alpha_out_ = nullptr;
  Tensor *forward_score_ = nullptr;
  Tensor *zero_ = nullptr;

  // CRF backward cell.
  Cell *backward_ = nullptr;
  Tensor *backward_primal_ = nullptr;
  Tensor *backward_logz_ = nullptr;
  Tensor *backward_beta_in_ = nullptr;
  Tensor *backward_beta_out_ = nullptr;
  //Tensor *backward_dinput_ = nullptr;
  Tensor *backward_p_ = nullptr;
  Tensor *backward_prev_curr_ = nullptr;

  // CRF likelihood cell.
  Cell *likelihood_ = nullptr;
  Tensor *likelihood_alpha_ = nullptr;
  Tensor *likelihood_score_ = nullptr;
  Tensor *likelihood_logz_ = nullptr;
  Tensor *likelihood_nll_ = nullptr;
};

}  // namespace myelin
}  // namespace sling

#endif  // SLING_MYELIN_CRF_H_


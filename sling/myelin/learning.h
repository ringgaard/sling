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

#ifndef SLING_MYELIN_LEARNING_H_
#define SLING_MYELIN_LEARNING_H_

#include <string>
#include <map>

#include "sling/myelin/builder.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/flow.h"

namespace sling {
namespace myelin {

// Cross entropy loss for multi-class classification.
class CrossEntropyLoss {
 public:
  CrossEntropyLoss(const string &name = "loss") : name_(name) {}
  ~CrossEntropyLoss() { delete profile_; }

  // Build loss function together with gradient computation.
  void Build(Flow *flow, Flow::Variable *logits, Flow::Variable *dlogits);

  // Initialize loss for model.
  void Initialize(const Network &network);

  // Compute loss from logits and output loss gradient.
  float Compute(float *logits, int target, float *dlogits);

  // Profile information.
  ProfileSummary *profile() const { return profile_; }

 private:
  // Name of loss function.
  string name_;

  // Cell for loss computation.
  Cell *cell_ = nullptr;

  // Tensors for loss computation.
  Tensor *logits_ = nullptr;
  Tensor *target_ = nullptr;
  Tensor *loss_ = nullptr;
  Tensor *dlogits_ = nullptr;

  // Profile information.
  ProfileSummary *profile_ = nullptr;
};

// A parameter optimizer applies updates to the learnable parameters of a model
// based on the (accumulated) gradients from backpropagation.
class Optimizer {
 public:
  // Mapping from learnable variables to their gradients.
  typedef std::map<Flow::Variable *, Flow::Variable *> GradientMap;

  Optimizer(const string &name = "optimizer") : name_(name) {}
  virtual ~Optimizer() { delete update_; delete profile_; }

  // Build update function for applying gradients.
  void Build(Flow *flow);

  // Initialize gradient update for model.
  void Initialize(const Network &network);

  // Apply gradients to update learnable parameters.
  void Apply(std::vector<Instance *> &gradients);

  // Profile information.
  ProfileSummary *profile() const { return profile_; }

 protected:
  // Let subclass build the parameter update using the gradient map.
  virtual void BuildOptimizer(const GradientMap &gradmap, Builder *update) = 0;

  // Let subclass initialize update function for optimizer.
  virtual void InitializeOptimizer() = 0;

  // Name of optimizer.
  string name_;

  // Mapping from gradient computation cell to instance variable in update.
  std::map<Flow::Function *, Flow::Variable *> instance_;
  std::map<const Cell *, Tensor *> refs_;

  // Instance for updating the learnable parameters from the gradients.
  Instance *update_ = nullptr;

  // Profile information.
  ProfileSummary *profile_ = nullptr;
};

// Stocastic gradient descent optimizer.
class GradientDescentOptimizer : public Optimizer {
 public:
  // Learning rate.
  float alpha() const { return *update_->Get<float>(alpha_); }
  void set_alpha(float alpha) { *update_->Get<float>(alpha_) = alpha; }

  // Regularization parameter for L2 regularization.
  float lambda() const { return lambda_; }
  void set_lambda(float lambda) { lambda_ = lambda; }

  // Norm clipping threshold.
  float clipping_threshold() const { return clipping_threshold_; }
  void set_clipping_threshold(float t) { clipping_threshold_ = t; }

 protected:
  void BuildOptimizer(const GradientMap &gradmap, Builder *update) override;
  void InitializeOptimizer() override;

  Tensor *alpha_ = nullptr;         // learning rate
  float clipping_threshold_ = 0.0;  // norm clipping threshold (0=no clipping)
  float lambda_ = 0.0;              // regularization parameter (0=none)
};

}  // namespace myelin
}  // namespace sling

#endif  // SLING_MYELIN_BUILDER_H_


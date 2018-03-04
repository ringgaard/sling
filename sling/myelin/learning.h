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
  CrossEntropyLoss(const string &name = "loss");

  // Build loss function together with gradient computation.
  void Build(Flow *flow, Flow::Variable *logits);

  // Initialize loss for model.
  void Initialize(const Network &network);

  // Batch for accumulating losses from forward computations.
  class Batch {
   public:
    // Initialize loss batch computation.
    Batch(const CrossEntropyLoss &loss);

    // Clear accumulated losses.
    void Clear();

    // Accumulate loss from logits.
    void Forward(float *logits, int target);

    // Compute loss gradient for batch.
    void Backward();

    // Return average loss for batch.
    float loss() { return *backward_.Get<float>(loss_.loss_); }

    // Return current batch size.
    int batch_size() { return *forward_.Get<int>(loss_.batch_size_); }

    // Return loss gradient.
    float *dlogits() { return backward_.Get<float>(loss_.dlogits_); }

   private:
    const CrossEntropyLoss &loss_;

    Instance forward_;   // forward computation and accumulation of losses
    Instance backward_;  // backward computation of gradient
  };

 private:
  // Name of loss function.
  string name_;

  // Name of gradient function for loss.
  string gradient_name_;

  // Cells for forward and backward loss computation.
  Cell *forward_ = nullptr;
  Cell *backward_ = nullptr;

  // Tensors for forward and backward loss computation.
  Tensor *logits_ = nullptr;
  Tensor *target_ = nullptr;
  Tensor *batch_size_ = nullptr;
  Tensor *primal_ = nullptr;
  Tensor *loss_ = nullptr;
  Tensor *dlogits_ = nullptr;
};

// A parameter optimizer applies updates to the learnable parameters of a model
// based on the (accumulated) gradients from backpropagation.
class Optimizer {
 public:
  // Mapping from learnable variables to their gradients.
  typedef std::map<Flow::Variable *, Flow::Variable *> GradientMap;

  Optimizer(const string &name = "optimizer") : name_(name) {}
  virtual ~Optimizer() { delete update_; }

  // Build update function for applying gradients.
  void Build(Flow *flow);

  // Initialize gradient update for model.
  void Initialize(const Network &network);

  // Apply gradients to update learnable parameters.
  void Apply(std::vector<Instance *> &gradients);

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
};

// Stocastic gradient descent optimizer.
class GradientDescentOptimizer : public Optimizer {
 public:
  // Return current learning rate.
  float alpha() const { return *update_->Get<float>(alpha_); }

  // Set learning rate.
  void set_alpha(float alpha) { *update_->Get<float>(alpha_) = alpha; }

 protected:
  void BuildOptimizer(const GradientMap &gradmap, Builder *update) override;
  void InitializeOptimizer() override;

  Tensor *alpha_ = nullptr;  // learning rate
};

}  // namespace myelin
}  // namespace sling

#endif  // SLING_MYELIN_BUILDER_H_


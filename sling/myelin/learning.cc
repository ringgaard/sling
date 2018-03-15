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

#include "sling/myelin/learning.h"

#include "sling/base/logging.h"
#include "sling/myelin/builder.h"

namespace sling {
namespace myelin {

// Return name of gradient variable.
static string GradientName(const string &name) {
  int slash = name.rfind('/');
  if (slash == -1) return "gradients/d_" + name;
  return "gradients/" + name.substr(0, slash) + "/d_" + name.substr(slash + 1);
}

void CrossEntropyLoss::Build(Flow *flow,
                             Flow::Variable *logits,
                             Flow::Variable *dlogits) {
  // Assume logits batch dimension is one.
  CHECK_EQ(logits->rank(), 2);
  CHECK_EQ(logits->dim(0), 1);
  CHECK(logits->shape == dlogits->shape);
  int size = logits->dim(1);

  // Build loss and loss gradient computation.
  Builder tf(flow, name_);

  // Inputs are logits and target label.
  auto *input = tf.Placeholder("logits", DT_FLOAT, logits->shape);
  input->ref = true;
  auto *target = tf.Placeholder("target", DT_INT32, {});

  // Compute softmax for logits.
  auto *softmax = tf.Softmax(tf.Reshape(input, {size}));

  // Compute loss (negative log-likelihood).
  auto *loss = tf.Name(tf.Neg(tf.Log(tf.Slice(softmax, target))), "loss");
  loss->flags |= Flow::Variable::OUT;

  // Compute gradient.
  auto *gradient = tf.Sub(softmax, tf.OneHot(target, size));
  auto *output = tf.Name(tf.Reshape(gradient, dlogits->shape), "d_logits");
  output->ref = true;

  // Connect input and output logits.
  auto *cnx = flow->AddConnector(name_ + "/cnx_logits");
  cnx->AddLink(logits);
  cnx->AddLink(input);
  auto *dcnx = flow->AddConnector(name_ + "/cnx_dlogits");
  dcnx->AddLink(dlogits);
  dcnx->AddLink(output);
}

void CrossEntropyLoss::Initialize(const Network &network) {
  // Get loss computation cell.
  cell_ = network.GetCell(name_);

  // Get tensors.
  logits_ = network.GetParameter(name_ + "/logits");
  target_ = network.GetParameter(name_ + "/target");
  loss_ = network.GetParameter(name_ + "/loss");
  dlogits_ = network.GetParameter(name_ + "/d_logits");

  // Create profile summary for profiling.
  if (cell_->profile()) profile_ = new ProfileSummary(cell_);
}

float CrossEntropyLoss::Compute(float *logits, int target, float *dlogits) {
  Instance data(cell_);
  data.SetReference(logits_, logits);
  data.SetReference(dlogits_, dlogits);
  *data.Get<int>(target_) = target;
  if (profile_) data.set_profile(profile_);
  data.Compute();
  return *data.Get<float>(loss_);
}

void Optimizer::Build(Flow *flow) {
  // Build mapping from learnable variable to gradient for variable.
  Builder tf(flow, name_);
  GradientMap gradmap;
  for (Flow::Variable *var : flow->vars()) {
    if (!var->learnable()) continue;

    // Get gradient variable for learnable variable.
    Flow::Variable *dvar = flow->Var(GradientName(var->name));
    CHECK(dvar != nullptr) << "No gradient found for " << var->name;

    // Find function for gradient variable.
    Flow::Operation *producer = nullptr;
    if (dvar->producer != nullptr) {
      producer = dvar->producer;
    } else if (!dvar->consumers.empty()) {
      producer = dvar->consumers[0];
    }
    CHECK(producer != nullptr) << "No producer for gradient " << dvar->name;
    Flow::Function *func = producer->func;
    CHECK(func != nullptr) "No producer function for gradient " << dvar->name;

    // Add instance variables for producer functions.
    if (instance_[func] == nullptr) instance_[func] = tf.Instance(func);

    // Add reference to gradient in update function.
    gradmap[var] = tf.Ref(instance_[func], dvar);
  }

  // Build optimizer.
  BuildOptimizer(gradmap, &tf);
}

void Optimizer::Initialize(const Network &network) {
  // Get cell for update.
  Cell *cell = network.GetCell(name_);

  // Create update instance.
  update_ = new Instance(cell);

  // Set up profiling.
  if (cell->profile()) profile_ = new ProfileSummary(cell);

  // Create mapping from gradient computation cell to instance variable in
  // update cell.
  for (auto it : instance_) {
    Cell *gradient_cell = network.GetCell(it.first->name);
    Tensor *gradient_instance = cell->GetParameter(it.second->name);
    refs_[gradient_cell] = gradient_instance;
  }

  // Initialize optimizer.
  InitializeOptimizer();
}

void Optimizer::Apply(std::vector<Instance *> &gradients) {
  // Set instance references to gradients in update.
  for (Instance *g : gradients) {
    auto f = refs_.find(g->cell());
    CHECK(f != refs_.end());
    update_->Set(f->second, g);
  }
  if (profile_) update_->set_profile(profile_);

  // Apply gradient update to learnable parameters.
  update_->Compute();
}

void GradientDescentOptimizer::BuildOptimizer(const GradientMap &gradmap,
                                              Builder *update) {
  // Add learning rate to update function.
  Builder &tf = *update;
  auto *alpha = tf.Var("alpha", DT_FLOAT, {});
  alpha->flags |= Flow::Variable::IN | Flow::Variable::OUT;
  auto *multiplier = tf.Neg(alpha);

  // Optionally add hyperparameter for gradient clipping.
  Flow::Variable *threshold = nullptr;
  if (clipping_threshold_ != 0.0) {
    threshold = tf.Name(tf.Const(clipping_threshold_), "threshold");
  }

  // Update learnable variables from gradients.
  for (auto it : gradmap) {
    auto *v = it.first;
    auto *dv = it.second;

    // Optionally add clipping.
    auto *weight = multiplier;
    if (threshold != nullptr) {
      // Compute L2 norm of threshold.
      auto *norm = tf.Norm(dv);
      //norm->flags |= Flow::Variable::OUT;

      // Compute clipping factor.
      auto *clip = tf.Div(threshold, tf.Max(norm, threshold));
      weight = tf.Mul(multiplier, clip);
    }

    auto *assign = tf.AssignAdd(v, tf.Mul(dv, weight));
    if (lambda_ != 0.0) {
      float decay = 1.0 - lambda_;
      assign->SetAttr("decay", decay);
    }
  }
}

void GradientDescentOptimizer::InitializeOptimizer() {
  // Set initial learning rate.
  alpha_ = update_->cell()->GetParameter(name_ + "/alpha");
  set_alpha(0.01);
}

}  // namespace myelin
}  // namespace sling


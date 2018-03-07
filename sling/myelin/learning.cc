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

CrossEntropyLoss::CrossEntropyLoss(const string &name) {
  name_ = name;
  gradient_name_ = "gradients/" + name;
}

void CrossEntropyLoss::Build(Flow *flow,
                             Flow::Variable *logits,
                             Flow::Variable *dlogits) {
  // Assume logits batch dimension is one.
  CHECK_EQ(logits->rank(), 2);
  CHECK_EQ(logits->dim(0), 1);
  CHECK(logits->shape == dlogits->shape);
  int size = logits->dim(1);

  // Build forward loss computation.
  Builder fwd(flow, name_);

  // Inputs are logits and target label.
  auto *prediction = fwd.Placeholder("logits", DT_FLOAT, logits->shape);
  prediction->ref = true;
  auto *target = fwd.Placeholder("target", DT_INT32, {});

  // Compute softmax for logits.
  auto *softmax = fwd.Softmax(fwd.Reshape(prediction, {size}));

  // Compute loss (negative log-likelihood).
  auto *loss = fwd.Neg(fwd.Log(fwd.Slice(softmax, target)));

  // Sum losses.
  auto *loss_sum = fwd.Placeholder("loss_sum", DT_FLOAT, {1});
  loss_sum->flags |= Flow::Variable::OUT;
  fwd.AssignAdd(loss_sum, loss);

  // Increment batch counter.
  auto *batch_size = fwd.Placeholder("batch_size", DT_FLOAT, {});
  batch_size->flags |= Flow::Variable::OUT;
  fwd.AssignAdd(batch_size, fwd.Const(1.0f));

  // Sum softmax.
  auto *softmax_sum = fwd.Placeholder("softmax_sum", DT_FLOAT, softmax->shape);
  softmax_sum->flags |= Flow::Variable::OUT;
  fwd.AssignAdd(softmax_sum, softmax);

  // Sum target labels.
  auto *labels = fwd.Placeholder("labels", DT_FLOAT, {size});
  labels->flags |= Flow::Variable::OUT;
  fwd.ScatterAdd(fwd.Reshape(labels, {size, 1}),
                 fwd.Reshape(target, {1, 1}),
                 fwd.Const(1.0f));

  // Build backward loss gradient.
  Builder bkw(flow, gradient_name_);
  auto *primal = bkw.Name(bkw.Instance(fwd.func()), "primal");
  auto *n = bkw.Ref(primal, batch_size);

  // Average loss.
  bkw.Name(bkw.Div(bkw.Ref(primal, loss_sum), n) , "loss");

  // Loss gradient.
  auto *diff = bkw.Sub(bkw.Ref(primal, softmax_sum), bkw.Ref(primal, labels));
  auto *mean = bkw.Div(diff ,n);
  auto *output = bkw.Name(bkw.Reshape(mean, {1, size}), "d_logits");

  // Connect input and output logits.
  auto *cnx = flow->AddConnector(name_ + "/cnx_logits");
  cnx->AddLink(logits);
  cnx->AddLink(prediction);
  auto *dcnx = flow->AddConnector(name_ + "/cnx_dlogits");
  dcnx->AddLink(dlogits);
  dcnx->AddLink(output);
}

void CrossEntropyLoss::Initialize(const Network &network) {
  // Get forward and backward loss computation cells.
  forward_ = network.GetCell(name_);
  backward_ = network.GetCell(gradient_name_);

  // Get tensors.
  logits_ = network.GetParameter(name_ + "/logits");
  target_ = network.GetParameter(name_ + "/target");
  batch_size_ = network.GetParameter(name_ + "/batch_size");
  primal_ = network.GetParameter(gradient_name_ + "/primal");
  loss_ = network.GetParameter(gradient_name_ + "/loss");
  dlogits_ = network.GetParameter(gradient_name_ + "/d_logits");
}

CrossEntropyLoss::Batch::Batch(const CrossEntropyLoss &loss)
    : loss_(loss), forward_(loss.forward_), backward_(loss.backward_) {
  if (loss.forward_->profile()) {
    forward_profile_ = new ProfileSummary(loss.forward_);
    forward_.set_profile(forward_profile_);
  }
  if (loss.backward_->profile()) {
    backward_profile_ = new ProfileSummary(loss.backward_);
    backward_.set_profile(backward_profile_);
  }
}

CrossEntropyLoss::Batch::~Batch() {
  delete forward_profile_;
  delete backward_profile_;
}

void CrossEntropyLoss::Batch::Clear() {
  forward_.Clear();
  backward_.Clear();
}

void CrossEntropyLoss::Batch::Forward(float *logits, int target) {
  forward_.SetReference(loss_.logits_, logits);
  *forward_.Get<int>(loss_.target_) = target;
  if (forward_profile_) forward_.set_profile(forward_profile_);
  forward_.Compute();
}

void CrossEntropyLoss::Batch::Backward() {
  backward_.Set(loss_.primal_, &forward_);
  if (backward_profile_) backward_.set_profile(backward_profile_);
  backward_.Compute();
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
  auto *alpha = update->Var("alpha", DT_FLOAT, {});
  alpha->flags |= Flow::Variable::IN | Flow::Variable::OUT;
  auto *weight = update->Neg(alpha);

  // Update learnable variables from gradients.
  for (auto it : gradmap) {
    auto *v = it.first;
    auto *dv = it.second;
    update->AssignAdd(v, update->Mul(dv, weight));
  }
}

void GradientDescentOptimizer::InitializeOptimizer() {
  // Set initial learning rate.
  alpha_ = update_->cell()->GetParameter(name_ + "/alpha");
  set_alpha(0.01);
}

}  // namespace myelin
}  // namespace sling


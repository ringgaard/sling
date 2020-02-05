// Copyright 2017 Google Inc.
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

#include "sling/nlp/parser/transition-decoder.h"

#include "sling/myelin/builder.h"
#include "sling/myelin/gradient.h"

namespace sling {
namespace nlp {

using namespace sling::myelin;

void TransitionDecoder::Setup(task::Task *task) {
  // Get training parameters.
  task->Fetch("mark_depth", &mark_depth_);
  task->Fetch("mark_dim", &mark_dim_);
  task->Fetch("frame_limit", &frame_limit_);
  task->Fetch("history_size", &history_size_);
  task->Fetch("out_roles_size", &out_roles_size_);
  task->Fetch("in_roles_size", &in_roles_size_);
  task->Fetch("labeled_roles_size", &labeled_roles_size_);
  task->Fetch("unlabeled_roles_size", &unlabeled_roles_size_);
  task->Fetch("roles_dim", &roles_dim_);
  task->Fetch("activations_dim", &activations_dim_);
  task->Fetch("link_dim_token", &link_dim_token_);
  task->Fetch("link_dim_step", &link_dim_step_);
  task->Fetch("ff_l2reg", &ff_l2reg_);
}

static Flow::Variable *LinkedFeature(
    FlowBuilder *f, const string &name, Flow::Variable *embeddings,
    int size, int dim) {
  int link_dim = embeddings->dim(1);
  auto *features = f->Placeholder(name, DT_INT32, {1, size});
  auto *oov = f->Parameter(name + "_oov", DT_FLOAT, {1, link_dim});
  auto *gather = f->Gather(embeddings, features, oov);
  auto *transform = f->Parameter(name + "_transform", DT_FLOAT,
                                 {link_dim, dim});
  f->RandomNormal(transform);
  return f->Reshape(f->MatMul(gather, transform), {1, size * dim});
}

void TransitionDecoder::Build(Flow *flow,  Flow::Variable *encodings, 
                              bool learn) {
  // Get token enmbedding dimensions.
  int token_dim = encodings->elements();

  // Build parser decoder.
  FlowBuilder f(flow, "decoder");
  std::vector<Flow::Variable *> features;

  // Add inputs for recurrent channels.
  auto *tokens = f.Placeholder("tokens", DT_FLOAT, {1, token_dim}, true);
  auto *steps = f.Placeholder("steps", DT_FLOAT, {1, activations_dim_}, true);

  // Role features.
  if (roles_.size() > 0 && in_roles_size_ > 0) {
    features.push_back(f.Feature("in_roles", roles_.size() * frame_limit_,
                                 in_roles_size_, roles_dim_));
  }
  if (roles_.size() > 0 && out_roles_size_ > 0) {
    features.push_back(f.Feature("out_roles", roles_.size() * frame_limit_,
                                 out_roles_size_, roles_dim_));
  }
  if (roles_.size() > 0 && labeled_roles_size_ > 0) {
    features.push_back(f.Feature("labeled_roles",
                                 roles_.size() * frame_limit_ * frame_limit_,
                                 labeled_roles_size_, roles_dim_));
  }
  if (roles_.size() > 0 && unlabeled_roles_size_ > 0) {
    features.push_back(f.Feature("unlabeled_roles",
                                 frame_limit_ * frame_limit_,
                                 unlabeled_roles_size_, roles_dim_));
  }

  // Link features.
  features.push_back(LinkedFeature(&f, "token", tokens, 1, link_dim_token_));
  features.push_back(LinkedFeature(&f, "attention_tokens",
                                   tokens, frame_limit_, link_dim_token_));
  features.push_back(LinkedFeature(&f, "attention_steps",
                                   steps, frame_limit_, link_dim_step_));
  features.push_back(LinkedFeature(&f, "history",
                                   steps, history_size_, link_dim_step_));

  // Mark features.
  features.push_back(LinkedFeature(&f, "mark_tokens",
                                   tokens, mark_depth_, link_dim_token_));
  features.push_back(LinkedFeature(&f, "mark_steps",
                                   steps, mark_depth_, link_dim_step_));

  // Pad feature vector.
  const static int alignment = 16;
  int n = 0;
  for (auto *f : features) n += f->elements();
  if (n % alignment != 0) {
    int padding = alignment - n % alignment;
    auto *zeroes = f.Const(nullptr, DT_FLOAT, {1, padding});
    features.push_back(zeroes);
  }

  // Concatenate mapped feature inputs.
  auto *fv = f.Concat(features);
  int fvsize = fv->dim(1);

  // Feed-forward layer.
  auto *W = f.Parameter("W0", DT_FLOAT, {fvsize, activations_dim_});
  auto *b = f.Parameter("b0", DT_FLOAT, {1, activations_dim_});
  f.RandomNormal(W);
  if (ff_l2reg_ != 0.0) W->SetAttr("l2reg", ff_l2reg_);
  auto *activation = f.Name(f.Relu(f.Add(f.MatMul(fv, W), b)), "activation");
  activation->set_in()->set_out()->set_ref();

  // Build function decoder gradient.
  Flow::Variable *dactivation = nullptr;
  if (learn) {
    Gradient(flow, f.func());
    dactivation = flow->GradientVar(activation);
  }

#if 0
  // Build flows for delegates.
  for (DelegateLearner *delegate : delegates_) {
    delegate->Build(flow, activation, dactivation, learn);
  }
#endif

  // Link recurrences.
  flow->Connect({tokens, encodings});
  flow->Connect({steps, activation});
  if (learn) {
    auto *dsteps = flow->GradientVar(steps);
    flow->Connect({dsteps, dactivation});
  }
}

void TransitionDecoder::Save(Flow *flow, Builder *spec) {
  spec->Set("type", "transition");
  spec->Set("frame_limit", frame_limit_);
  spec->Set("sentence_reset", sentence_reset_);

  Handles role_list(spec->store());
  roles_.GetList(&role_list);
  spec->Set("roles", Array(spec->store(), role_list));

#if 0
  Array delegates(spec->store(), delegates_.size());
  for (int i = 0; i < delegates_.size(); ++i) {
    Builder delegate_spec(&store);
    delegates_[i]->Save(&flow, &delegate_spec);
    delegates.set(i, delegate_spec.Create().handle());
  }
  spec->Set("delegates", delegates);
#endif
}

void TransitionDecoder::Load(Flow *flow, const Frame &spec) {
}

void TransitionDecoder::Initialize(const Network &net) {
  // Get decoder cells and tensors.
  decoder_ = net.GetCell("decoder");
  encodings_ = decoder_->GetParameter("decoder/tokens");
  activations_ = decoder_->GetParameter("decoder/steps");
  activation_ = decoder_->GetParameter("decoder/activation");

  gdecoder_ = decoder_->Gradient();
  primal_ = decoder_->Primal();
  dencodings_ = encodings_->Gradient();
  dactivations_ = activations_->Gradient();
  dactivation_ = activation_->Gradient();
  //for (auto *d : delegates_) d->Initialize(model_);

  // Initialize feature model,
  feature_model_.Init(decoder_, &roles_, frame_limit_);
}

}  // namespace nlp
}  // namespace sling


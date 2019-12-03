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

#include "sling/myelin/rnn.h"

#include "sling/myelin/builder.h"
#include "sling/myelin/gradient.h"

namespace sling {
namespace myelin {

RNN::Variables RNN::Build(Flow *flow,
                          Flow::Variable *input,
                          Flow::Variable *dinput) {
  Variables vars;

  // Build RNN cell.
  FlowBuilder f(flow, name);
  vars.input = f.Placeholder("input", input->type, input->shape, true);
  switch (type) {
    case DRAGNN_LSTM:
      vars.output = f.LSTMLayer(vars.input, dim);
      break;
    default:
      LOG(FATAL) << "RNN type not supported: " << type;
  }

  // Make zero element.
  auto *zero = f.Name(f.Const(nullptr, input->type, {1, dim}), "zero");
  zero->set_out();
  flow->Connect({vars.output, zero});

  // Connect input to RNN.
  flow->Connect({vars.input, input});

  // Build gradients for learning.
  if (dinput != nullptr) {
    auto *gf = Gradient(flow, f.func());
    vars.dinput = flow->GradientVar(vars.input);
    vars.doutput = flow->GradientVar(vars.output);
    flow->Connect({vars.dinput, dinput});

    // Make sink variable for final channel gradients.
    auto *sink = f.Var("sink", input->type, {1, dim})->set_out();
    gf->unused.push_back(sink);
    flow->Connect({sink, flow->Var(gf->name + "/d_h_out")});
  }

  return vars;
}

void RNN::Initialize(const Network &net) {
  // Initialize RNN cell. Control channel is optional.
  cell = net.GetCell(name);
  input = net.GetParameter(name + "/input");
  h_in = net.GetParameter(name + "/h_in");
  h_out = net.GetParameter(name + "/h_out");
  c_in = net.LookupParameter(name + "/c_in");
  c_out = net.LookupParameter(name + "/c_out");
  zero = net.GetParameter(name + "/zero");

  // Initialize gradient cell for RNN.
  gcell = cell->Gradient();
  if (gcell != nullptr) {
    primal = cell->Primal();
    dinput = input->Gradient();
    dh_in = h_in->Gradient();
    dh_out = h_out->Gradient();
    dc_in = c_in == nullptr ? nullptr : c_in->Gradient();
    dc_out = c_out == nullptr ? nullptr : c_out->Gradient();
    sink = net.GetParameter(name + "/sink");
  }
}

RNNMerger::Variables RNNMerger::Build(Flow *flow,
                                      Flow::Variable *left,
                                      Flow::Variable *right,
                                      Flow::Variable *dleft,
                                      Flow::Variable *dright) {
  Variables vars;

  // Build merger cell.
  FlowBuilder f(flow, name);
  vars.left = f.Placeholder("left", left->type, left->shape);
  vars.left->set_dynamic()->set_unique();

  vars.right = f.Placeholder("right", right->type, right->shape);
  vars.right->set_dynamic()->set_unique();

  vars.merged = f.Name(f.Concat({vars.left, vars.right}, 1), "merged");
  vars.merged->set_dynamic();
  flow->Connect({vars.left, left});
  flow->Connect({vars.right, right});

  // Build gradients for learning.
  if (dleft != nullptr && dright != nullptr) {
    Gradient(flow, f.func());
    vars.dmerged = flow->GradientVar(vars.merged);
    vars.dleft = flow->GradientVar(vars.left);
    vars.dright = flow->GradientVar(vars.right);
    flow->Connect({vars.dleft, dleft});
    flow->Connect({vars.dright, dright});
  }

  return vars;
}

void RNNMerger::Initialize(const Network &net) {
  cell = net.GetCell(name);
  left = net.GetParameter(name + "/left");
  right = net.GetParameter(name + "/right");
  merged = net.GetParameter(name + "/merged");

  gcell = cell->Gradient();
  if (gcell != nullptr) {
    dmerged = merged->Gradient();
    dleft = left->Gradient();
    dright = right->Gradient();
  }
}

RNNLayer::RNNLayer(const string &name, RNN::Type type, int dim, bool bidir)
    : name_(name),
      bidir_(bidir),
      lr_(name + "/lr", type, dim),
      rl_(name + "/rl", type, dim),
      merger_(name) {
  if (!bidir) lr_.name = name;
}

RNN::Variables RNNLayer::Build(Flow *flow,
                               Flow::Variable *input,
                               Flow::Variable *dinput) {
  if (bidir_) {
    // Build left-to-right and right-to-left RNNs.
    auto l = lr_.Build(flow, input, dinput);
    auto r = rl_.Build(flow, input, dinput);

    // Build channel merger.
    auto m = merger_.Build(flow, l.output, r.output, l.doutput, r.output);

    // Return outputs.
    RNN::Variables vars;
    vars.input = l.input;
    vars.output = m.merged;
    vars.dinput = l.dinput;
    vars.doutput = m.dmerged;

    return vars;
  } else {
    return lr_.Build(flow, input, dinput);
  }
}

void RNNLayer::Initialize(const Network &net) {
  lr_.Initialize(net);
  if (bidir_) {
    rl_.Initialize(net);
    merger_.Initialize(net);
  }
}

RNNInstance::RNNInstance(const RNNLayer *rnn)
    : rnn_(rnn),
      lr_(rnn->lr_.cell),
      lr_hidden_(rnn->lr_.h_out),
      lr_control_(rnn->lr_.c_out),
      rl_(rnn->rl_.cell),
      rl_hidden_(rnn->rl_.h_out),
      rl_control_(rnn->rl_.c_out),
      merger_(rnn->merger_.cell),
      merged_(rnn->merger_.merged) {}

Channel *RNNInstance::Compute(Channel *input) {
  // Get sequence length.
  int length = input->size();
  bool ctrl = rnn_->lr_.has_control();

  // Compute left-to-right RNN.
  lr_hidden_.resize(length);
  if (ctrl) lr_control_.resize(length);

  if (length > 0) {
    lr_.Set(rnn_->lr_.input, input, 0);
    lr_.SetReference(rnn_->lr_.h_in, rnn_->lr_.zero->data());
    lr_.Set(rnn_->lr_.h_out, &lr_hidden_, 0);
    if (ctrl) {
      lr_.SetReference(rnn_->lr_.c_in, rnn_->lr_.zero->data());
      lr_.Set(rnn_->lr_.c_out, &lr_control_, 0);
    }
    lr_.Compute();
  }

  for (int i = 1; i < length; ++i) {
    lr_.Set(rnn_->lr_.input, input, i);
    lr_.Set(rnn_->lr_.h_in, &lr_hidden_, i - 1);
    lr_.Set(rnn_->lr_.h_out, &lr_hidden_, i);
    if (ctrl) {
      lr_.Set(rnn_->lr_.c_in, &lr_control_, i - 1);
      lr_.Set(rnn_->lr_.c_out, &lr_control_, i);
    }
    lr_.Compute();
  }

  // Return left-to-right hidden channel for unidirectional RNN.
  if (!rnn_->bidir_) return &lr_hidden_;

  // Compute right-to-left RNN.
  rl_hidden_.resize(length);
  if (ctrl) rl_control_.resize(length);

  if (length > 0) {
    rl_.Set(rnn_->rl_.input, input, length - 1);
    rl_.SetReference(rnn_->rl_.h_in, rnn_->rl_.zero->data());
    rl_.Set(rnn_->rl_.h_out, &rl_hidden_, length - 1);
    if (ctrl) {
      rl_.SetReference(rnn_->rl_.c_in, rnn_->rl_.zero->data());
      rl_.Set(rnn_->rl_.c_out, &rl_control_, length - 1);
    }
    rl_.Compute();
  }

  for (int i = length - 2; i >= 0; --i) {
    rl_.Set(rnn_->rl_.input, input, i);
    rl_.Set(rnn_->rl_.h_in, &rl_hidden_, i + 1);
    rl_.Set(rnn_->rl_.h_out, &rl_hidden_, i);
    if (ctrl) {
      rl_.Set(rnn_->rl_.c_in, &rl_control_, i + 1);
      rl_.Set(rnn_->rl_.c_out, &rl_control_, i);
    }
    rl_.Compute();
  }

  // Merge outputs.
  merged_.resize(length);
  merger_.SetChannel(rnn_->merger_.left, &lr_hidden_);
  merger_.SetChannel(rnn_->merger_.right, &rl_hidden_);
  merger_.SetChannel(rnn_->merger_.merged, &merged_);
  merger_.Compute();

  return &merged_;
}

RNNLearner::RNNLearner(const RNNLayer *rnn)
    : rnn_(rnn),
      lr_fwd_(rnn->lr_.cell),
      lr_hidden_(rnn->lr_.h_out),
      lr_control_(rnn->lr_.c_out),
      lr_bkw_(rnn->lr_.gcell),
      lr_dhidden_(rnn->lr_.dh_in),
      lr_dcontrol_(rnn->lr_.dc_in),
      rl_fwd_(rnn->rl_.cell),
      rl_hidden_(rnn->rl_.h_out),
      rl_control_(rnn->rl_.c_out),
      rl_bkw_(rnn->rl_.gcell),
      rl_dhidden_(rnn->rl_.dh_in),
      rl_dcontrol_(rnn->rl_.dc_in),
      dinput_(rnn_->rl_.dinput),
      merger_(rnn->merger_.cell),
      splitter_(rnn->merger_.gcell),
      merged_(rnn->merger_.merged),
      dleft_(rnn->merger_.dleft),
      dright_(rnn->merger_.dright) {}

Channel *RNNLearner::Compute(Channel *input) {
  // Get sequence length.
  int length = input->size();
  bool ctrl = rnn_->lr_.has_control();

  // Compute left-to-right RNN.
  lr_fwd_.resize(length);
  lr_hidden_.resize(length);
  if (ctrl) lr_control_.resize(length);

  if (length > 0) {
    Instance &data = lr_fwd_[0];
    data.Set(rnn_->lr_.input, input, 0);
    data.SetReference(rnn_->lr_.h_in, rnn_->lr_.zero->data());
    data.Set(rnn_->lr_.h_out, &lr_hidden_, 0);
    if (ctrl) {
      data.SetReference(rnn_->lr_.c_in, rnn_->lr_.zero->data());
      data.Set(rnn_->lr_.c_out, &lr_control_, 0);
    }
    data.Compute();
  }

  for (int i = 1; i < length; ++i) {
    Instance &data = lr_fwd_[i];
    data.Set(rnn_->lr_.input, input, i);
    data.Set(rnn_->lr_.h_in, &lr_hidden_, i - 1);
    data.Set(rnn_->lr_.h_out, &lr_hidden_, i);
    if (ctrl) {
      data.Set(rnn_->lr_.c_in, &lr_control_, i - 1);
      data.Set(rnn_->lr_.c_out, &lr_control_, i);
    }
    data.Compute();
  }

  // Return left-to-right hidden channel for unidirectional RNN.
  if (!rnn_->bidir_) return &lr_hidden_;

  // Compute right-to-left RNN.
  rl_fwd_.resize(length);
  rl_hidden_.resize(length);
  if (ctrl) rl_control_.resize(length);

  if (length > 0) {
    Instance &data = rl_fwd_[length - 1];
    data.Set(rnn_->rl_.input, input, length - 1);
    data.SetReference(rnn_->rl_.h_in, rnn_->rl_.zero->data());
    data.Set(rnn_->rl_.h_out, &rl_hidden_, length - 1);
    if (ctrl) {
      data.SetReference(rnn_->rl_.c_in, rnn_->rl_.zero->data());
      data.Set(rnn_->rl_.c_out, &rl_control_, length - 1);
    }
    data.Compute();
  }

  for (int i = length - 2; i >= 0; --i) {
    Instance &data = rl_fwd_[i];
    data.Set(rnn_->rl_.input, input, i);
    data.Set(rnn_->rl_.h_in, &rl_hidden_, i + 1);
    data.Set(rnn_->rl_.h_out, &rl_hidden_, i);
    if (ctrl) {
      data.Set(rnn_->rl_.c_in, &rl_control_, i + 1);
      data.Set(rnn_->rl_.c_out, &rl_control_, i);
    }
    data.Compute();
  }

  // Merge outputs.
  merged_.resize(length);
  merger_.SetChannel(rnn_->merger_.left, &lr_hidden_);
  merger_.SetChannel(rnn_->merger_.right, &rl_hidden_);
  merger_.SetChannel(rnn_->merger_.merged, &merged_);
  merger_.Compute();

  return &merged_;
}

Channel *RNNLearner::Backpropagate(Channel *doutput) {
  // Clear input gradient.
  int length = doutput->size();
  dinput_.reset(length);
  bool ctrl = rnn_->lr_.has_control();

  // Split gradient for bidirectional RNN.
  Channel *dleft;
  Channel *dright;
  if (rnn_->bidir_) {
    // Split gradients.
    dleft_.resize(length);
    dright_.resize(length);
    splitter_.SetChannel(rnn_->merger_.dmerged, doutput);
    splitter_.SetChannel(rnn_->merger_.dleft, &dleft_);
    splitter_.SetChannel(rnn_->merger_.dright, &dright_);
    splitter_.Compute();
    dleft = &dleft_;
    dright = &dright_;
  } else {
    dleft = doutput;
    dright = nullptr;
  }

  // Propagate gradients for left-to-right RNN.
  if (dleft != nullptr) {
    if (ctrl) lr_dcontrol_.reset(length);
    for (int i = length - 1; i >= 2; --i) {
      lr_bkw_.Set(rnn_->lr_.primal, &lr_fwd_[i]);
      lr_bkw_.Set(rnn_->lr_.dh_out, dleft, i);
      lr_bkw_.Set(rnn_->lr_.dh_in, dleft, i - 1);
      lr_bkw_.Set(rnn_->lr_.dinput, &dinput_, i);
      if (ctrl) {
        lr_bkw_.Set(rnn_->lr_.dc_out, &lr_dcontrol_, i);
        lr_bkw_.Set(rnn_->lr_.dc_in, &lr_dcontrol_, i - 1);
      }
      lr_bkw_.Compute();
    }

    if (length > 0) {
      void *sink = lr_bkw_.GetAddress(rnn_->lr_.sink);
      lr_bkw_.Set(rnn_->lr_.primal, &lr_fwd_[0]);
      lr_bkw_.Set(rnn_->lr_.dh_out, dleft, 0);
      lr_bkw_.SetReference(rnn_->lr_.dh_in, sink);
      lr_bkw_.Set(rnn_->lr_.dinput, &dinput_, 0);
      if (ctrl) {
        lr_bkw_.Set(rnn_->lr_.dc_out, &lr_dcontrol_, 0);
        lr_bkw_.SetReference(rnn_->lr_.dc_in, sink);
      }
      lr_bkw_.Compute();
    }
  }

  // Propagate gradients for right-to-left RNN.
  if (dright != nullptr) {
    if (ctrl) rl_dcontrol_.reset(length);
    for (int i = 0; i < length - 1; ++i) {
      rl_bkw_.Set(rnn_->rl_.primal, &rl_fwd_[i]);
      rl_bkw_.Set(rnn_->rl_.dh_out, dright, i);
      rl_bkw_.Set(rnn_->rl_.dh_in, dright, i + 1);
      rl_bkw_.Set(rnn_->rl_.dinput, &dinput_, i);
      if (ctrl) {
        rl_bkw_.Set(rnn_->rl_.dc_out, &rl_dcontrol_, i);
        rl_bkw_.Set(rnn_->rl_.dc_in, &rl_dcontrol_, i + 1);
      }
      rl_bkw_.Compute();
    }

    if (length > 0) {
      void *sink = rl_bkw_.GetAddress(rnn_->rl_.sink);
      rl_bkw_.Set(rnn_->rl_.primal, &rl_fwd_[length - 1]);
      rl_bkw_.Set(rnn_->rl_.dh_out, dright, length - 1);
      rl_bkw_.SetReference(rnn_->rl_.dh_in, sink);
      rl_bkw_.Set(rnn_->rl_.dinput, &dinput_, length - 1);
      if (ctrl) {
        rl_bkw_.Set(rnn_->rl_.dc_out, &rl_dcontrol_, length - 1);
        rl_bkw_.SetReference(rnn_->rl_.dc_in, sink);
      }
      rl_bkw_.Compute();
    }
  }

  // Return input gradient.
  return &dinput_;
}

void RNNLearner::Clear() {
  lr_bkw_.Clear();
  if (rnn_->bidir_) rl_bkw_.Clear();
}

void RNNLearner::CollectGradients(std::vector<Instance *> *gradients) {
  gradients->push_back(&lr_bkw_);
  if (rnn_->bidir_) gradients->push_back(&rl_bkw_);
}

void RNNStack::AddLayer(RNN::Type type, int dim, bool bidir) {
  string name = name_ + "/rnn" + std::to_string(layers_.size());
  layers_.emplace_back(name, type, dim, bidir);
}

void RNNStack::AddLayers(int layers, RNN::Type type, int dim, bool bidir) {
  for (int l = 0; l < layers; ++l) {
    AddLayer(type, dim, bidir);
  }
}

RNN::Variables RNNStack::Build(Flow *flow,
                               Flow::Variable *input,
                               Flow::Variable *dinput) {
  RNN::Variables vars;
  for (RNNLayer &l : layers_) {
    RNN::Variables v = l.Build(flow, input, dinput);
    if (vars.input == nullptr) {
      vars.input = v.input;
      vars.dinput = v.dinput;
    }
    vars.output = v.output;
    vars.doutput = v.doutput;
    input = v.output;
    dinput = v.doutput;
  }
  return vars;
}

void RNNStack::Initialize(const Network &net) {
  for (RNNLayer &l : layers_) {
    l.Initialize(net);
  }
}

RNNStackInstance::RNNStackInstance(const RNNStack &stack) {
  layers_.reserve(stack.layers().size());
  for (const RNNLayer &l : stack.layers()) {
    layers_.emplace_back(&l);
  }
}

Channel *RNNStackInstance::Compute(Channel *input) {
  Channel *channel = input;
  for (RNNInstance &l : layers_) {
    channel = l.Compute(channel);
  }
  return channel;
}

RNNStackLearner::RNNStackLearner(const RNNStack &stack) {
  layers_.reserve(stack.layers().size());
  for (const RNNLayer &l : stack.layers()) {
    layers_.emplace_back(&l);
  }
}

Channel *RNNStackLearner::Compute(Channel *input) {
  Channel *channel = input;
  for (RNNLearner &l : layers_) {
    channel = l.Compute(channel);
  }
  return channel;
}

Channel *RNNStackLearner::Backpropagate(Channel *doutput) {
  Channel *channel = doutput;
  for (int i = layers_.size() - 1; i >= 0; --i) {
    channel = layers_[i].Backpropagate(channel);
  }
  return channel;
}

void RNNStackLearner::Clear() {
  for (RNNLearner &l : layers_) {
    l.Clear();
  }
}

void RNNStackLearner::CollectGradients(std::vector<Instance *> *gradients) {
  for (RNNLearner &l : layers_) {
    l.CollectGradients(gradients);
  }
}

}  // namespace myelin
}  // namespace sling


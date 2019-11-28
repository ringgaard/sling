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

  // Connect input to RNN.
  flow->Connect({vars.input, input, zero});

  // Build gradients for learning.
  if (dinput != nullptr) {
    auto *gf = Gradient(flow, f.func());
    vars.dinput = flow->GradientVar(vars.input);
    vars.doutput = flow->GradientVar(vars.output);
    flow->Connect({vars.dinput, dinput});

    // Make sink variable for final channel gradients.
    auto *sink = f.Var("sink", input->type, {1, dim})->set_out();
    gf->unused.push_back(sink);
    flow->Connect({sink, flow->Var(gf->name + "/dh_out")});
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
  vars.left = f.Placeholder("left", left->type, left->shape, true);
  vars.left->set_dynamic();

  vars.right = f.Placeholder("right", right->type, right->shape, true);
  vars.right->set_dynamic();

  vars.merged = f.Name(f.Concat({vars.left, vars.right}, 2), "merged");
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
      lr_hidden_(rnn->lr_.h_out),
      lr_control_(rnn->lr_.c_out),
      lr_bkw_(rnn->lr_.gcell),
      lr_dhidden_(rnn->lr_.dh_in),
      lr_dcontrol_(rnn->lr_.dc_in),
      rl_hidden_(rnn->rl_.h_out),
      rl_control_(rnn->rl_.c_out),
      rl_bkw_(rnn->rl_.gcell),
      rl_dhidden_(rnn->rl_.dh_in),
      rl_dcontrol_(rnn->rl_.dc_in),
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
  if (lr_fwd_.size() > length) lr_fwd_.resize(length);
  while (lr_fwd_.size() < length) lr_fwd_.emplace_back(rnn_->lr_.cell);

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
  if (rl_fwd_.size() > length) rl_fwd_.resize(length);
  while (rl_fwd_.size() < length) rl_fwd_.emplace_back(rnn_->rl_.cell);

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
  // TODO: implement
  return nullptr;
}

void RNNLearner::Clear() {
  lr_bkw_.Clear();
  if (rnn_->bidir_) rl_bkw_.Clear();
}

void RNNLearner::CollectGradients(std::vector<Instance *> *gradients) {
  gradients->push_back(&lr_bkw_);
  if (rnn_->bidir_) gradients->push_back(&rl_bkw_);
}

#if 0
// Forward RNN instance for prediction.
class ForwardRNNInstance : public RNNInstance {
 public:
  ForwardRNNInstance(const RNN &rnn)
    : rnn_(rnn),
      data_(rnn.cell),
      hidden_(rnn.h_out),
      control_(rnn.c_out) {}

  Channel *Compute(Channel *input) override {
    // Reset hidden and control channels.
    int length = input->size();
    bool ctrl = rnn_.has_control();
    hidden_.resize(length);
    if (ctrl) control_.resize(length);

    // Compute first RNN cell.
    if (length > 0) {
      data_.Set(rnn_.input, input, 0);
      data_.SetReference(rnn_.h_in, rnn_.zero->data());
      data_.Set(rnn_.h_out, &hidden_, 0);
      if (ctrl) {
        data_.SetReference(rnn_.c_in, rnn_.zero->data());
        data_.Set(rnn_.c_out, &control_, 0);
      }
      data_.Compute();
    }

    // Compute remaining RNN cells left-to-right.
    for (int i = 1; i < length; ++i) {
      data_.Set(rnn_.input, input, i);
      data_.Set(rnn_.h_in, &hidden_, i - 1);
      data_.Set(rnn_.h_out, &hidden_, i);
      if (ctrl) {
        data_.Set(rnn_.c_in, &control_, i - 1);
        data_.Set(rnn_.c_out, &control_, i);
      }
      data_.Compute();
    }

    return &hidden_;
  }

 private:
  const RNN &rnn_;           // RNN cell
  Instance data_;            // RNN instance data
  Channel hidden_;           // hidden channel
  Channel control_;          // control channel (optional)
};

// Forward RNN instance for learning.
class ForwardRNNLearner : public RNNLearner {
 public:
  ForwardRNNLearner(const RNN &rnn)
    : rnn_(rnn),
      bkw_(rnn.gcell),
      hidden_(rnn.h_out),
      control_(rnn.c_out),
      dcontrol_(rnn.dc_in),
      dinput_(rnn.dinput) {}

  ~ForwardRNNLearner() {
    for (Instance *data : fwd_) delete data;
  }

  Channel *Compute(Channel *input) override {
    // Allocate instances.
    int length = input->size();
    for (auto *data : fwd_) delete data;
    fwd_.resize(length);
    for (int i = 0; i < length; ++i) {
      fwd_[i] = new Instance(rnn_.cell);
    }

    // Reset hidden and control channels.
    bool ctrl = rnn_.has_control();
    hidden_.resize(length);
    if (ctrl) control_.resize(length);

    // Compute first RNN cell.
    if (length > 0) {
      Instance *data = fwd_[0];
      data->Set(rnn_.input, input, 0);
      data->SetReference(rnn_.h_in, rnn_.zero->data());
      data->Set(rnn_.h_out, &hidden_, 0);
      if (ctrl) {
        data->SetReference(rnn_.c_in, rnn_.zero->data());
        data->Set(rnn_.c_out, &control_, 0);
      }
      data->Compute();
    }

    // Compute remaining RNN cells left-to-right.
    for (int i = 1; i < length; ++i) {
      Instance *data = fwd_[i];
      data->Set(rnn_.input, input, i);
      data->Set(rnn_.h_in, &hidden_, i - 1);
      data->Set(rnn_.h_out, &hidden_, i);
      if (ctrl) {
        data->Set(rnn_.c_in, &control_, i - 1);
        data->Set(rnn_.c_out, &control_, i);
      }
      data->Compute();
    }

    return &hidden_;
  }

  Channel *Backpropagate(Channel *doutput) override {
    // Clear input gradient.
    int length = fwd_.size();
    bool ctrl = rnn_.has_control();
    dinput_.reset(length);
    if (ctrl) dcontrol_.reset(length);

    // Propagate gradients right-to-left.
    for (int i = length - 1; i >= 2; --i) {
      bkw_.Set(rnn_.primal, fwd_[i]);
      bkw_.Set(rnn_.dh_out, doutput, i);
      bkw_.Set(rnn_.dh_in, doutput, i - 1);
      bkw_.Set(rnn_.dinput, &dinput_, i);
      if (ctrl) {
        bkw_.Set(rnn_.dc_out, &dcontrol_, i);
        bkw_.Set(rnn_.dc_in, &dcontrol_, i - 1);
      }
      bkw_.Compute();
    }

    // Propagate gradients for first element.
    if (length > 0) {
      bkw_.Set(rnn_.primal, fwd_[0]);
      bkw_.Set(rnn_.dh_out, doutput, 0);
      bkw_.SetReference(rnn_.dh_in, bkw_.GetAddress(rnn_.sink));
      bkw_.Set(rnn_.dinput, &dinput_, 0);
      if (ctrl) {
        bkw_.Set(rnn_.dc_out, &dcontrol_, 0);
        bkw_.SetReference(rnn_.dc_in, bkw_.GetAddress(rnn_.sink));
      }
      bkw_.Compute();
    }

    // Return input gradient.
    return &dinput_;
  }

  void Clear() override {
    bkw_.Clear();
  }

  void CollectGradients(std::vector<Instance *> *gradients) override {
    gradients->push_back(&bkw_);
  }

 private:
  const RNN &rnn_;                // RNN cell

  std::vector<Instance *> fwd_;   // RNN forward instances
  Instance bkw_;                  // RNN gradients

  Channel hidden_;                // RNN hidden channel
  Channel control_;               // RNN control channel (optional)
  Channel dcontrol_;              // RNN control gradient channel
  Channel dinput_;                // input gradient channel
};

// Forward RNN layer.
class ForwardRNNLayer : public RNNLayer {
 public:
  ForwardRNNLayer(const string &name, RNN::Type type, int dim)
      : rnn_(name, type, dim) {}

  RNN::Variables Build(Flow *flow,
                       Flow::Variable *input,
                       Flow::Variable *dinput) override {
    return rnn_.Build(flow, input, dinput);
  }

  void Initialize(const Network &net) override {
    rnn_.Initialize(net);
  }

  RNNInstance *CreateInstance() override {
    return new ForwardRNNInstance(rnn_);
  }

  RNNLearner *CreateLearner() override {
    return new ForwardRNNLearner(rnn_);
  }

 private:
  // RNN cell.
  RNN rnn_;
};

// Reverse RNN instance for prediction.
class ReverseRNNInstance : public RNNInstance {
 public:
  ReverseRNNInstance(const RNN &rnn)
    : rnn_(rnn),
      data_(rnn.cell),
      hidden_(rnn.h_out),
      control_(rnn.c_out) {}

  Channel *Compute(Channel *input) override {
    // Reset hidden and control channels.
    int length = input->size();
    bool ctrl = rnn_.has_control();
    hidden_.resize(length);
    if (ctrl) control_.resize(length);

    // Compute last RNN cell.
    if (length > 0) {
      data_.Set(rnn_.input, input, length - 1);
      data_.SetReference(rnn_.h_in, rnn_.zero->data());
      data_.Set(rnn_.h_out, &hidden_, length - 1);
      if (ctrl) {
        data_.SetReference(rnn_.c_in, rnn_.zero->data());
        data_.Set(rnn_.c_out, &control_, length - 1);
      }
      data_.Compute();
    }

    // Compute remaining RNN cells right-to-left.
    for (int i = length - 2; i >= 0; --i) {
      data_.Set(rnn_.input, input, i);
      data_.Set(rnn_.h_in, &hidden_, i + 1);
      data_.Set(rnn_.h_out, &hidden_, i);
      if (ctrl) {
        data_.Set(rnn_.c_in, &control_, i + 1);
        data_.Set(rnn_.c_out, &control_, i);
      }
      data_.Compute();
    }

    return &hidden_;
  }

 private:
  const RNN &rnn_;           // RNN cell
  Instance data_;            // RNN instance data
  Channel hidden_;           // hidden channel
  Channel control_;          // control channel (optional)
};

// Reverse RNN instance for learning.
class ReverseRNNLearner : public RNNLearner {
 public:
  ReverseRNNLearner(const RNN &rnn)
    : rnn_(rnn),
      bkw_(rnn.gcell),
      hidden_(rnn.h_out),
      control_(rnn.c_out),
      dcontrol_(rnn.dc_in),
      dinput_(rnn.dinput) {}

  Channel *Compute(Channel *input) override {
    // Allocate instances.
    int length = input->size();
    for (auto *data : fwd_) delete data;
    fwd_.resize(length);
    for (int i = 0; i < length; ++i) {
      fwd_[i] = new Instance(rnn_.cell);
    }

    // Reset hidden and control channels.
    bool ctrl = rnn_.has_control();
    hidden_.resize(length);
    if (ctrl) control_.resize(length);

    // Compute last RNN cell.
    if (length > 0) {
      Instance *data = fwd_[length - 1];
      data->Set(rnn_.input, input, length - 1);
      data->SetReference(rnn_.h_out, rnn_.zero->data());
      data->Set(rnn_.h_in, &hidden_, length - 1);
      if (ctrl) {
        data->SetReference(rnn_.c_out, rnn_.zero->data());
        data->Set(rnn_.c_in, &control_, length - 1);
      }
      data->Compute();
    }

    // Compute remaining RNN cells right-to-left.
    for (int i = length - 2; i >= 0; --i) {
      Instance *data = fwd_[i];
      data->Set(rnn_.input, input, i);
      data->Set(rnn_.h_out, &hidden_, i + 1);
      data->Set(rnn_.h_in, &hidden_, i);
      if (ctrl) {
        data->Set(rnn_.c_out, &control_, i + 1);
        data->Set(rnn_.c_in, &control_, i);
      }
      data->Compute();
    }

    return &hidden_;
  }

  Channel *Backpropagate(Channel *doutput) override {
    // Clear input gradient.
    int length = fwd_.size();
    bool ctrl = rnn_.has_control();
    dinput_.reset(length);
    if (ctrl) dcontrol_.reset(length);

    // Propagate gradients left-to-right.
    for (int i = 0; i < length - 1; ++i) {
      bkw_.Set(rnn_.primal, fwd_[i]);
      bkw_.Set(rnn_.dh_out, doutput, i);
      bkw_.Set(rnn_.dh_in, doutput, i + 1);
      bkw_.Set(rnn_.dinput, &dinput_, i);
      if (ctrl) {
        bkw_.Set(rnn_.dc_out, &dcontrol_, i);
        bkw_.Set(rnn_.dc_in, &dcontrol_, i + 1);
      }
      bkw_.Compute();
    }

    // Propagate gradients for last element.
    if (length > 0) {
      bkw_.Set(rnn_.primal, fwd_[length - 1]);
      bkw_.Set(rnn_.dh_out, doutput, length - 1);
      bkw_.SetReference(rnn_.dh_in, bkw_.GetAddress(rnn_.sink));
      bkw_.Set(rnn_.dinput, &dinput_, length - 1);
      if (ctrl) {
        bkw_.Set(rnn_.dc_out, &dcontrol_, length - 1);
        bkw_.SetReference(rnn_.dc_in, bkw_.GetAddress(rnn_.sink));
      }
      bkw_.Compute();
    }

    // Return input gradient.
    return &dinput_;
  }

  void Clear() override {
    bkw_.Clear();
  }

  void CollectGradients(std::vector<Instance *> *gradients) override {
    gradients->push_back(&bkw_);
  }

 private:
  const RNN &rnn_;                // RNN cell

  std::vector<Instance *> fwd_;   // RNN forward instances
  Instance bkw_;                  // RNN gradients

  Channel hidden_;                // RNN hidden channel
  Channel control_;               // RNN control channel (optional)
  Channel dcontrol_;              // RNN control gradient channel
  Channel dinput_;                // input gradient channel
};

// Reverse RNN layer.
class ReverseRNNLayer : public RNNLayer {
 public:
  ReverseRNNLayer(const string &name, RNN::Type type, int dim)
      : rnn_(name, type, dim) {}

  RNN::Variables Build(Flow *flow,
                       Flow::Variable *input,
                       Flow::Variable *dinput) override {
    return rnn_.Build(flow, input, dinput);
  }

  void Initialize(const Network &net) override {
    rnn_.Initialize(net);
  }

  RNNInstance *CreateInstance() override {
    return new ReverseRNNInstance(rnn_);
  }

  RNNLearner *CreateLearner() override {
    return new ReverseRNNLearner(rnn_);
  }

 private:
  // RNN cell.
  RNN rnn_;
};

// Channel merger cell for merging the outputs from two RNNs.
struct ChannelMerger {
  // Flow input/output variables.
  struct Variables {
    Flow::Variable *left;     // left input to forward path
    Flow::Variable *right;    // right input to forward path
    Flow::Variable *merged;   // merged output from forward path

    Flow::Variable *dmerged;  // merged gradient from backward path
    Flow::Variable *dleft;    // left gradient output from backward path
    Flow::Variable *dright;   // right gradient output from backward path
  };

  // Initialize channel merger.
  ChannelMerger(const string &name) : name(name) {}

  // Build flow for channel merger. If dleft and dright are not null, the
  // corresponding gradient function is also built.
  Variables Build(Flow *flow,
                  Flow::Variable *left, Flow::Variable *right,
                  Flow::Variable *dleft, Flow::Variable *dright) {
    Variables vars;

    // Build merger cell.
    FlowBuilder f(flow, name);
    vars.left = f.Placeholder("left", left->type, left->shape, true);
    vars.left->set_dynamic();

    vars.right = f.Placeholder("right", right->type, right->shape, true);
    vars.right->set_dynamic();

    vars.merged = f.Name(f.Concat({vars.left, vars.right}, 2), "merged");
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

  // Initialize channel merger.
  void Initialize(const Network &net) {
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

  string name;                     // cell name

  Cell *cell = nullptr;            // merger cell
  Tensor *left = nullptr;          // left channel input
  Tensor *right = nullptr;         // right channel input
  Tensor *merged = nullptr;        // merged output channel

  Cell *gcell = nullptr;           // merger gradient cell
  Tensor *dmerged = nullptr;       // gradient for merged channel
  Tensor *dleft = nullptr;         // gradient for left channel
  Tensor *dright = nullptr;        // gradient for right channel
};

// Bidirectional RNN instance for prediction.
class BiRNNInstance : public RNNInstance {
 public:
  BiRNNInstance(const RNN &lr, const RNN &rl, const ChannelMerger &merger)
    : lr_(lr),
      rl_(rl),
      merger_(merger),
      data_(merger.cell),
      merged_(merger.merged) {}

  Channel *Compute(Channel *input) override {
    // Compute left-to-right and right-to-left RNNs.
    Channel *l = lr_.Compute(input);
    Channel *r = rl_.Compute(input);

    // Merge outputs.
    data_.SetChannel(merger_.left, l);
    data_.SetChannel(merger_.right, r);
    data_.SetChannel(merger_.merged, &merged_);
    data_.Compute();

    return &merged_;
  }

 private:
  ForwardRNNInstance lr_;        // forward RNN
  ReverseRNNInstance rl_;        // reverse RNN

  const ChannelMerger &merger_;  // channel merger cell
  Instance data_;                // channel merger instance
  Channel merged_;               // channel with merged output
};

// Bidirectional RNN instance for learning.
class BiRNNLearner : public RNNLearner {
 public:
  BiRNNLearner(const RNN &lr, const RNN &rl, const ChannelMerger &cm)
    : lr_(lr),
      rl_(rl),
      cm_(cm),
      merger_(cm.cell),
      splitter_(cm.gcell),
      merged_(cm.merged),
      dleft_(cm.dleft),
      dright_(cm.dright) {}

  Channel *Compute(Channel *input) override {
    // Compute left-to-right and right-to-left RNNs.
    Channel *l = lr_.Compute(input);
    Channel *r = rl_.Compute(input);

    // Merge outputs.
    merger_.SetChannel(cm_.left, l);
    merger_.SetChannel(cm_.right, r);
    merger_.SetChannel(cm_.merged, &merged_);
    merger_.Compute();

    return &merged_;
  }

  Channel *Backpropagate(Channel *doutput) override {
    // Split gradients.
    splitter_.SetChannel(cm_.dmerged, doutput);
    splitter_.SetChannel(cm_.dleft, &dleft_);
    splitter_.SetChannel(cm_.dright, &dright_);
    splitter_.Compute();

    // Backpropagate through RNNs.
    Channel *dinput = lr_.Backpropagate(&dleft_);
    rl_.Backpropagate(&dright_);

    // Return input gradient.
    return dinput;
  }

  void Clear() override {
    lr_.Clear();
    rl_.Clear();
  }

  void CollectGradients(std::vector<Instance *> *gradients) override {
    lr_.CollectGradients(gradients);
    rl_.CollectGradients(gradients);
  }

 private:
  ForwardRNNLearner lr_;        // forward RNN
  ReverseRNNLearner rl_;        // reverse RNN

  const ChannelMerger &cm_;     // channel merger cell
  Instance merger_;             // channel merger instance
  Instance splitter_;           // gradient channel splitter instance
  Channel merged_;              // channel with merged output
  Channel dleft_;               // channel with left gradient output
  Channel dright_;              // channel with right gradient output
};

// Bidirectional RNN layer.
class BiRNNLayer : public RNNLayer {
 public:
  BiRNNLayer(const string &name, RNN::Type type, int dim)
      : lr_(name + "/lr", type, dim),
        rl_(name + "/rl", type, dim),
        merger_(name) {}

  RNN::Variables Build(Flow *flow,
                       Flow::Variable *input,
                       Flow::Variable *dinput) override {
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
  }

  void Initialize(const Network &net) override {
    lr_.Initialize(net);
    rl_.Initialize(net);
    merger_.Initialize(net);
  }

  RNNInstance *CreateInstance() override {
    return new BiRNNInstance(lr_, rl_, merger_);
  }

  RNNLearner *CreateLearner() override {
    return new BiRNNLearner(lr_, rl_, merger_);
  }

 private:
  RNN lr_;                // cell for left-to-right RNN
  RNN rl_;                // cell for right-to-left RNN
  ChannelMerger merger_;  // cell for channel merger
};

// RNN stack instance for prediction.
class RNNStackInstance : public RNNInstance {
 public:
  RNNStackInstance(const RNNStack &stack) {
    layers_.reserve(stack.layers().size());
    for (RNNLayer *l : stack.layers()) {
      layers_.push_back(l->CreateInstance());
    }
  }

  Channel *Compute(Channel *input) override {
    Channel *channel = input;
    for (RNNInstance *l : layers_) {
      channel = l->Compute(channel);
    }
    return channel;
  }

 private:
  std::vector<RNNInstance *> layers_;
};

// RNN stack instance for learning.
class RNNStackLearner : public RNNLearner {
 public:
  RNNStackLearner(const RNNStack &stack) {
    layers_.reserve(stack.layers().size());
    for (RNNLayer *l : stack.layers()) {
      layers_.push_back(l->CreateLearner());
    }
  }

  Channel *Compute(Channel *input) override {
    Channel *channel = input;
    for (RNNLearner *l : layers_) {
      channel = l->Compute(channel);
    }
    return channel;
  }

  Channel *Backpropagate(Channel *doutput) override {
    Channel *channel = doutput;
    for (int i = layers_.size() - 1; i >= 0; --i) {
      channel = layers_[i]->Backpropagate(channel);
    }
    return channel;
  }

  void Clear() override {
    for (RNNLearner *l : layers_) {
      l->Clear();
    }
  }

  void CollectGradients(std::vector<Instance *> *gradients) override {
    for (RNNLearner *l : layers_) {
      l->CollectGradients(gradients);
    }
  }

 private:
  std::vector<RNNLearner *> layers_;
};

RNNStack::~RNNStack() {
  for (RNNLayer *l : layers_) delete l;
}

void RNNStack::AddLayer(RNN::Type type, int dim, RNN::Direction dir) {
  string layer_name = name_ + "/rnn" + std::to_string(layers_.size());
  RNNLayer *layer;
  switch (dir) {
    case RNN::FORWARD:
      layer = new ForwardRNNLayer(layer_name, type, dim);
      break;
    case RNN::REVERSE:
      layer = new ReverseRNNLayer(layer_name, type, dim);
      break;
    case RNN::BIDIR:
      layer = new BiRNNLayer(layer_name, type, dim);
      break;
    default:
      LOG(FATAL) << "Unsupported RNN direction";
  }
  layers_.push_back(layer);
}

void RNNStack::AddLayers(int layers, RNN::Type type, int dim,
                         RNN::Direction dir) {
  for (int l = 0; l < layers; ++l) {
    AddLayer(type, dim, dir);
  }
}

RNN::Variables RNNStack::Build(Flow *flow,
                               Flow::Variable *input,
                               Flow::Variable *dinput) {
  RNN::Variables vars;
  for (RNNLayer *l : layers_) {
    RNN::Variables v = l->Build(flow, input, dinput);
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
  for (RNNLayer *l : layers_) {
    l->Initialize(net);
  }
}

RNNInstance *RNNStack::CreateInstance() {
  return new RNNStackInstance(*this);
}

RNNLearner *RNNStack::CreateLearner() {
  return new RNNStackLearner(*this);
}

#endif

}  // namespace myelin
}  // namespace sling


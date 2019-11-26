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

RNN::Outputs RNN::Build(Flow *flow, Type type, int dim,
                        Flow::Variable *input,
                        Flow::Variable *dinput) {
  Outputs out;

  // Build RNN cell.
  FlowBuilder f(flow, name);
  auto *in = f.Placeholder("input", input->type, input->shape, true);
  switch (type) {
    case DRAGNN_LSTM:
      out.hidden = f.LSTMLayer(in, dim);
      break;
    default:
      LOG(FATAL) << "RNN type not supported: " << type;
  }

  // Make zero element.
  auto *zero = f.Name(f.Const(nullptr, input->type, {1, dim}), "zero");
  zero->set_out();
  flow->Connect({out.hidden, zero});

  // Connect input to RNN.
  flow->Connect({input, in});

  // Build gradients for learning.
  if (dinput != nullptr) {
    auto *gf = Gradient(flow, f.func());
    out.dinput = flow->GradientVar(in);
    flow->Connect({dinput, out.dinput});

    // Make sink variable for final channel gradients.
    auto *sink = f.Var("sink", input->type, {1, dim})->set_out();
    gf->unused.push_back(sink);
    flow->Connect({sink, flow->Var(gf->name + "/dh_out")});
  } else {
    out.dinput = nullptr;
  }

  return out;
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
      dhidden_(rnn.dh_in),
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

  void CollectGradients(std::vector<Instance *> *gradients) override {
    gradients->push_back(&bkw_);
  }

  Channel *GetGradient() override {
    int length = fwd_.size();
    dhidden_.reset(length);
    if (rnn_.has_control()) dcontrol_.reset(length);
    return &dhidden_;
  }

  Channel *Backpropagate() override {
    // Clear input gradient.
    int length = fwd_.size();
    dinput_.reset(length);
    bool ctrl = rnn_.has_control();

    // Propagate gradients right-to-left.
    for (int i = length - 1; i >= 2; --i) {
      bkw_.Set(rnn_.primal, fwd_[i]);
      bkw_.Set(rnn_.dh_out, &dhidden_, i);
      bkw_.Set(rnn_.dh_in, &dhidden_, i - 1);
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
      bkw_.Set(rnn_.dh_out, &dhidden_, 0);
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

 private:
  const RNN &rnn_;                // RNN cell

  std::vector<Instance *> fwd_;   // RNN forward instances
  Instance bkw_;                  // RNN gradients

  Channel hidden_;                // RNN hidden channel
  Channel control_;               // RNN control channel (optional)

  Channel dhidden_;               // RNN hidden gradient channel
  Channel dcontrol_;              // RNN control gradient channel

  Channel dinput_;                // input gradient channel
};

// Forward RNN layer.
class ForwardRNNLayer : public RNNLayer {
 public:
  ForwardRNNLayer(const string &name) : rnn_(name) {}

  RNN::Outputs Build(Flow *flow, RNN::Type type, int dim,
                     Flow::Variable *input,
                     Flow::Variable *dinput) override {
    return rnn_.Build(flow, type, dim, input, dinput);
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
      dhidden_(rnn.dh_in),
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

  void CollectGradients(std::vector<Instance *> *gradients) override {
    gradients->push_back(&bkw_);
  }

  Channel *GetGradient() override {
    int length = fwd_.size();
    dhidden_.reset(length);
    if (rnn_.has_control()) dcontrol_.reset(length);
    return &dhidden_;
  }

  Channel *Backpropagate() override {
    // Clear input gradient.
    int length = fwd_.size();
    dinput_.reset(length);
    bool ctrl = rnn_.has_control();

    // Propagate gradients left-to-right.
    for (int i = 0; i < length - 1; ++i) {
      bkw_.Set(rnn_.primal, fwd_[i]);
      bkw_.Set(rnn_.dh_out, &dhidden_, i);
      bkw_.Set(rnn_.dh_in, &dhidden_, i + 1);
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
      bkw_.Set(rnn_.dh_out, &dhidden_, length - 1);
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

 private:
  const RNN &rnn_;                // RNN cell

  std::vector<Instance *> fwd_;   // RNN forward instances
  Instance bkw_;                  // RNN gradients

  Channel hidden_;                // RNN hidden channel
  Channel control_;               // RNN control channel (optional)

  Channel dhidden_;               // RNN hidden gradient channel
  Channel dcontrol_;              // RNN control gradient channel

  Channel dinput_;                // input gradient channel
};

// Reverse RNN layer.
class ReverseRNNLayer : public RNNLayer {
 public:
  ReverseRNNLayer(const string &name) : rnn_(name) {}

  RNN::Outputs Build(Flow *flow, RNN::Type type, int dim,
                     Flow::Variable *input,
                     Flow::Variable *dinput) override {
    return rnn_.Build(flow, type, dim, input, dinput);
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
  // Flow output variables.
  struct Outputs {
    Flow::Variable *merged;  // merged output from forward path
    Flow::Variable *dleft;   // left gradient output from backward path
    Flow::Variable *dright;  // right gradient output from backward path
  };

  // Initialize channel merger.
  ChannelMerger(const string &name) : name(name) {}

  // Build flow for channel merger. If dleft and dright is not null, the
  // corresponding gradient function is also built.
  Outputs Build(Flow *flow,
                Flow::Variable *left, Flow::Variable *right,
                Flow::Variable *dleft, Flow::Variable *dright) {
    Outputs out;

    // Build merger cell.
    FlowBuilder f(flow, name);
    auto *l = f.Placeholder("left", left->type, left->shape, true);
    l->set_dynamic();
    auto *r = f.Placeholder("right", right->type, right->shape, true);
    r->set_dynamic();
    out.merged = f.Name(f.Concat({l, r}, 2), "merged");
    out.merged->set_dynamic();

    // Build gradients for learning.
    if (dleft != nullptr && dright != nullptr) {
      Gradient(flow, f.func());
      out.dleft = flow->GradientVar(l);
      out.dright = flow->GradientVar(r);
      flow->Connect({dleft, out.dleft});
      flow->Connect({dright, out.dright});
    } else {
      out.dleft = nullptr;
      out.dright = nullptr;
    }

    return out;
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
      dmerged_(cm.dmerged) {}

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

  void CollectGradients(std::vector<Instance *> *gradients) override {
    lr_.CollectGradients(gradients);
    rl_.CollectGradients(gradients);
  }

  Channel *GetGradient() override {
    int length = merged_.size();
    dmerged_.reset(length);
    return &dmerged_;
  }

  Channel *Backpropagate() override {
    // Split gradients.
    Channel *l = lr_.GetGradient();
    Channel *r = rl_.GetGradient();
    splitter_.SetChannel(cm_.dmerged, &dmerged_);
    splitter_.SetChannel(cm_.dleft, l);
    splitter_.SetChannel(cm_.dright, r);
    splitter_.Compute();

    // Backpropagate through RNNs.
    Channel *dinput = lr_.Backpropagate();
    rl_.Backpropagate();

    // Return input gradient.
    return dinput;
  }

  void Clear() override {
    lr_.Clear();
    rl_.Clear();
  }

 private:
  ForwardRNNLearner lr_;        // forward RNN
  ReverseRNNLearner rl_;        // reverse RNN

  const ChannelMerger &cm_;     // channel merger cell
  Instance merger_;             // channel merger instance
  Instance splitter_;           // gradient channel splitter instance
  Channel merged_;              // channel with merged output
  Channel dmerged_;             // channel with merged gradients
};

// Bidirectional RNN layer.
class BiRNNLayer : public RNNLayer {
 public:
  BiRNNLayer(const string &name)
      : lr_(name + "/lr"),
        rl_(name + "/rl"),
        merger_(name) {}

  RNN::Outputs Build(Flow *flow, RNN::Type type, int dim,
                     Flow::Variable *input,
                     Flow::Variable *dinput) override {
    // Build left-to-right and right-to-left RNNs.
    RNN::Outputs l = lr_.Build(flow, type, dim, input, dinput);
    RNN::Outputs r = rl_.Build(flow, type, dim, input, dinput);
    flow->Connect({l.dinput, r.dinput});

    // Build channel merger.
    ChannelMerger::Outputs m = merger_.Build(flow,
        l.hidden, r.hidden,
        flow->GradientVar(l.hidden), flow->GradientVar(r.hidden));

    // Return outputs.
    RNN::Outputs out;
    out.hidden = m.merged;
    out.dinput = l.dinput;
    return out;
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

RNNStack::~RNNStack() {
  for (auto &l : layers_) delete l.factory;
}

void RNNStack::AddLayer(RNN::Type type, int dim, RNN::Direction dir) {
  string layer_name = name_ + "/rnn" + std::to_string(layers_.size());
  RNNLayer *factory;
  switch (dir) {
    case RNN::FORWARD: factory = new ForwardRNNLayer(layer_name); break;
    case RNN::REVERSE: factory = new ReverseRNNLayer(layer_name); break;
    case RNN::BIDIR: factory = new BiRNNLayer(layer_name); break;
    default: LOG(FATAL) << "Unsupported RNN direction";
  }
  layers_.emplace_back(factory, type, dim);
}

void RNNStack::AddLayers(int layers, RNN::Type type, int dim,
                         RNN::Direction dir) {
  for (int l = 0; l < layers; ++l) {
    AddLayer(type, dim, dir);
  }
}

RNN::Outputs RNNStack::Build(Flow *flow,
                             Flow::Variable *input,
                             Flow::Variable *dinput) {
  RNN::Outputs out;
  //for (Layer &l : layers_) {
    // TODO: implemnent
  //}
  return out;
}

}  // namespace myelin
}  // namespace sling


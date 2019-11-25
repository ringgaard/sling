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

  // Make zero and sink elements for channels.
  auto *zero = f.Name(f.Const(nullptr, input->type, {1, dim}), "zero");
  auto *sink = f.Name(f.Const(nullptr, input->type, {1, dim}), "sink");
  flow->Connect({out.hidden, zero, sink});

  // Connect input to RNN.
  flow->Connect({input, in});

  // Build gradients for learning.
  if (dinput != nullptr) {
    Gradient(flow, f.func());
    out.dinput = flow->GradientVar(in);
    flow->Connect({dinput, out.dinput});
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
  sink = net.GetParameter(name + "/sink");

  // Initialize gradient cell for RNN.
  gcell = cell->Gradient();
  if (gcell != nullptr) {
    primal = cell->Primal();
    dinput = input->Gradient();
    dh_in = h_in->Gradient();
    dh_out = h_out->Gradient();
    dc_in = c_in == nullptr ? nullptr : c_in->Gradient();
    dc_out = c_out == nullptr ? nullptr : c_out->Gradient();
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
      bkw_.SetReference(rnn_.dh_in, rnn_.sink->data());
      bkw_.Set(rnn_.dinput, &dinput_, 0);
      if (ctrl) {
        bkw_.Set(rnn_.dc_out, &dcontrol_, 0);
        bkw_.SetReference(rnn_.dc_in, rnn_.sink->data());
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
      bkw_.SetReference(rnn_.dh_in, rnn_.sink->data());
      bkw_.Set(rnn_.dinput, &dinput_, length - 1);
      if (ctrl) {
        bkw_.Set(rnn_.dc_out, &dcontrol_, length - 1);
        bkw_.SetReference(rnn_.dc_in, rnn_.sink->data());
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

}  // namespace myelin
}  // namespace sling


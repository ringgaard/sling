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

#ifndef SLING_MYELIN_RNN_H_
#define SLING_MYELIN_RNN_H_

#include <vector>

#include "sling/myelin/compute.h"
#include "sling/myelin/flow.h"

namespace sling {
namespace myelin {

// Recurrent Neural Network (RNN) cell.
struct RNN {
  // RNN types.
  enum Type {
    LSTM,         // vanilla LSTM
    DRAGNN_LSTM,  // DRAGNN-variant of LSTM
  };

  // RNN direction.
  enum Direction {FORWARD, REVERSE, BIDIR};

  // Flow output variables.
  struct Outputs {
    Flow::Variable *hidden;  // output from forward path
    Flow::Variable *dinput;  // gradient output from backward path
  };

  // Initialize RNN.
  RNN(const string &name = "rnn") : name(name) {}

  // Build flow for RNN. If dinput is not null, the corresponding gradient
  // function is also built.
  Outputs Build(Flow *flow, Type type, int dim,
                Flow::Variable *input,
                Flow::Variable *dinput = nullptr);

  // Initialize RNN.
  void Initialize(const Network &net);

  // Control channel is optional for RNN.
  bool has_control() const { return c_in != nullptr; }

  string name;                     // RNN cell name

  Cell *cell = nullptr;            // RNN cell
  Tensor *input = nullptr;         // RNN feature input
  Tensor *h_in = nullptr;          // link to RNN hidden input
  Tensor *h_out = nullptr;         // link to RNN hidden output
  Tensor *c_in = nullptr;          // link to RNN control input
  Tensor *c_out = nullptr;         // link to RNN control output
  Tensor *zero = nullptr;          // zero element for channels

  Cell *gcell = nullptr;           // RNN gradient cell
  Tensor *dinput = nullptr;        // input gradient
  Tensor *primal = nullptr;        // link to primal RNN cell
  Tensor *dh_in = nullptr;         // gradient for RNN hidden input
  Tensor *dh_out = nullptr;        // gradient for RNN hidden output
  Tensor *dc_in = nullptr;         // gradient for RNN control input
  Tensor *dc_out = nullptr;        // gradient for RNN control output
  Tensor *sink = nullptr;          // scratch element for channels
};

// Interface for instance of RNN layer for prediction.
class RNNInstance {
 public:
  virtual ~RNNInstance() = default;

  // Compute RNN over input sequence and return output sequence.
  virtual Channel *Compute(Channel *input) = 0;
};

// Interface for instance of RNN layer for learning.
class RNNLearner : public RNNInstance {
 public:
  // Collect instances with gradient updates.
  virtual void CollectGradients(std::vector<Instance *> *gradients) = 0;

  // Get channel with input to backpropagation, i.e. gradient of output
  // sequence.
  virtual Channel *GetGradient() = 0;

  // Backpropagate gradients returning the output of backpropagation, i.e. the
  // gradient of the input sequence.
  virtual Channel *Backpropagate() = 0;

  // Clear accumulated gradients.
  virtual void Clear() = 0;
};

// Factory interface for making RNN instances for prediction and learning.
class RNNLayer {
 public:
  virtual ~RNNLayer() = default;

  // Build flow for RNN. If dinput is not null, the corresponding gradient
  // function is also built.
  virtual RNN::Outputs Build(Flow *flow, RNN::Type type, int dim,
                             Flow::Variable *input,
                             Flow::Variable *dinput) = 0;

  // Initialize RNN.
  virtual void Initialize(const Network &net) = 0;

  // Create RNN instance for prediction.
  virtual RNNInstance *CreateInstance() = 0;

  // Create RNN instance for learning.
  virtual RNNLearner *CreateLearner() = 0;
};

// Multi-layer RNN.
class RNNStack {
 public:
  RNNStack(const string &name) : name_(name) {}
  ~RNNStack();

  // Add RNN layer.
  void AddLayer(RNN::Type type, int dim, RNN::Direction dir);

  // Add multiple RNN layers.
  void AddLayers(int layers, RNN::Type type, int dim, RNN::Direction dir);

  // Build flow for RNNs.
  RNN::Outputs Build(Flow *flow,
                     Flow::Variable *input,
                     Flow::Variable *dinput = nullptr);

 private:
  struct Layer {
    Layer(RNNLayer *factory, RNN::Type type, int dim)
        : factory(factory), type(type), dim(dim) {}

    RNNLayer *factory;
    RNN::Type type;
    int dim;
  };

  string name_;
  std::vector<Layer> layers_;
};

}  // namespace myelin
}  // namespace sling

#endif  // SLING_MYELIN_RNN_H_


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

  // Flow input/output variables.
  struct Variables {
    Flow::Variable *input = nullptr;    // input to forward path
    Flow::Variable *output = nullptr;   // output from forward path
    Flow::Variable *doutput = nullptr;  // gradient input to backward path
    Flow::Variable *dinput = nullptr;   // gradient output from backward path
  };

  // Initialize RNN.
  RNN(const string &name, Type type, int dim) 
      : name(name), type(type), dim(dim) {}

  // Build flow for RNN. If dinput is not null, the corresponding gradient
  // function is also built.
  Variables Build(Flow *flow, 
                  Flow::Variable *input, 
                  Flow::Variable *dinput = nullptr);

  // Initialize RNN.
  void Initialize(const Network &net);

  // Control channel is optional for RNN.
  bool has_control() const { return c_in != nullptr; }

  string name;                     // RNN cell name
  Type type;                       // RNN type
  int dim;                         // RNN dimension

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
  // Backpropagate gradients returning the output of backpropagation, i.e. the
  // gradient of the input sequence.
  virtual Channel *Backpropagate(Channel *doutput) = 0;

  // Clear accumulated gradients.
  virtual void Clear() = 0;

  // Collect instances with gradient updates.
  virtual void CollectGradients(std::vector<Instance *> *gradients) = 0;
};

// Factory interface for making RNN instances for prediction and learning.
class RNNLayer {
 public:
  virtual ~RNNLayer() = default;

  // Build flow for RNN. If dinput is not null, the corresponding gradient
  // function is also built.
  virtual RNN::Variables Build(Flow *flow, 
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

  // Add multiple RNN layers of the same type.
  void AddLayers(int layers, RNN::Type type, int dim, RNN::Direction dir);

  // Build flow for RNNs.
  RNN::Variables Build(Flow *flow,
                       Flow::Variable *input,
                       Flow::Variable *dinput = nullptr);

  // Initialize RNN stack.
  void Initialize(const Network &net);

  // Create RNN stack instance for prediction.
  RNNInstance *CreateInstance();

  // Create RNN stack instance for learning.
  RNNLearner *CreateLearner();

  // Layers in RNN stack.
  const std::vector<RNNLayer *> layers() const { return layers_; }
 private:
  // Name prefix for RNN cells.
  string name_;
  
  // RNN layers.
  std::vector<RNNLayer *> layers_;
};

}  // namespace myelin
}  // namespace sling

#endif  // SLING_MYELIN_RNN_H_


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

#ifndef SLING_NLP_EMBEDDING_EMBEDDING_FLOW_H_
#define SLING_NLP_EMBEDDING_EMBEDDING_FLOW_H_

#include "sling/myelin/flow.h"

namespace sling {
namespace nlp {

// Embedding model with input layer, hidden layer, and output layer.
struct EmbeddingsFlow : public myelin::Flow {
  void Init(int inputs, int outputs, int dims, int features);

  int inputs;            // number of input dimensions
  int outputs;           // number of output dimensions
  int dims;              // number of dimensions in embedding vectors
  int features;          // (maximum) number of input features

  Variable *W0;          // input embedding matrix
  Variable *W1;          // output embedding matrix

  Variable *fv;          // input feature vector
  Variable *hidden;      // hidden activation

  Variable *alpha;       // learning rate
  Variable *label;       // output label (1=positive, 0=negative example)
  Variable *target;      // output target

  Variable *error;       // accumulated error

  Function *layer0;      // layer 0 forward computation
  Function *layer1;      // layer 1 forward/backward computation
  Function *layer0b;     // layer 0 backward computation

  Variable *l1_l0;       // reference to layer 0 in layer 1
  Variable *l0b_l0;      // reference to layer 0 in layer 0b
  Variable *l0b_l1;      // reference to layer 1 in layer 0b

 private:
  // Create embedding matrices.
  void BuildModel();

  // Build layer 0 computing hidden from input.
  void BuildLayer0();

  // Build layer 1 computing output from hidden and update layer 1 weights.
  void BuildLayer1();

  // Update layer 0 weights from accumulated error in layer 1.
  void BuildLayer0Back();
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_EMBEDDING_EMBEDDING_FLOW_H_


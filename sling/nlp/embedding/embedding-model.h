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

#include <vector>

#include "sling/myelin/flow.h"

namespace sling {
namespace nlp {

// Embedding model for training vector embeddings using the word2vec algorithm.
// This uses a network with an input layer, a hidden layer (the embedding), and
// an output layer, and optimizes the following objective function:
//
// L = \sum_i y_i log(sigmoid(a)) + (1-y_i) log(sigmoid(-a)), where
//   y_i \in {0, 1} is the item label
//   a = <w0_i, w1_o>
//   w0_i is the average of input embeddings w0_i
//   w1_o is the average of output embeddings w1_o
//
// See Mikolov et al. 2013 for more details.
struct EmbeddingsFlow : public myelin::Flow {
  void Init();

  int inputs = 0;         // number of input dimensions
  int outputs = 0;        // number of output dimensions
  int dims = 64;          // number of dimensions in embedding vectors
  int in_features = 32;   // (maximum) number of input features
  int out_features = 1;   // (maximum) number of output features

  Variable *W0;           // input embedding matrix
  Variable *W1;           // output embedding matrix

  Variable *fv;           // input feature vector
  Variable *hidden;       // hidden activation

  Variable *alpha;        // learning rate
  Variable *label;        // output label (1=positive, 0=negative example)
  Variable *target;       // output target

  Variable *loss;         // loss for example
  Variable *error;        // accumulated error

  Function *layer0;       // layer 0 forward computation
  Function *layer1;       // layer 1 forward/backward computation
  Function *layer0b;      // layer 0 backward computation

  Variable *l1_l0;        // reference to layer 0 in layer 1
  Variable *l0b_l0;       // reference to layer 0 in layer 0b
  Variable *l0b_l1;       // reference to layer 1 in layer 0b

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

// Distribution over weighted elements which can be sampled according to the
// weights.
class Distribution {
 public:
  // Add element to distribution.
  void Add(int index, float weight) {
    permutation_.emplace_back(index, weight);
  }

  // Shuffle elements and prepare for sampling.
  void Shuffle();

  // Sample from distribution.
  int Sample(float p) const;

 private:
  // Element in distribution. Before shuffling the probability field holds the
  // (unnormalized) weight for the element, but after shuffling it is the
  // cumulative probability.
  struct Element {
    Element(int i, float p) : index(i), probability(p) {}
    int index;
    float probability;
  };

  // Permutation of elements for sampling.
  std::vector<Element> permutation_;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_EMBEDDING_EMBEDDING_FLOW_H_


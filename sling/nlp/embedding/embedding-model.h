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
struct MikolovFlow : public myelin::Flow {
  void Build();

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

// Dual encoder network for learning embeddings over two different domains.
struct DualEncoderFlow : public myelin::Flow {
  struct Encoder {
    string name;                   // encoder name space
    int dims = 1;                  // number of dimensions (feature types)
    int max_features = 1;          // maximum number of features per example
    Variable *embeddings;          // embedding matrix
    Function *forward = nullptr;   // forward encoder computation
    Function *backward = nullptr;  // backward encoder computation
    Variable *features = nullptr;  // encoder feature input
    Variable *encoding = nullptr;  // encoder output
  };

  void Build(const myelin::Transformations &library);

  string name = "dualenc";         // model name space
  Encoder left;                    // left encoder
  Encoder right;                   // right encoder
  int dims = 64;                   // dimension of embedding vectors
  int batch_size = 1024;           // number of examples per batch

  Function *similarity = nullptr;      // similarity function
  Function *gsimilarity = nullptr;     // similarity gradient function
  Variable *left_encodings = nullptr;  // left encodings input
  Variable *right_encodings = nullptr; // right encodings input
  Variable *similarities = nullptr;    // similarity matrix
  Variable *gsimilarities = nullptr;   // similarity gradient matrix

 private:
  void BuildEncoder(Encoder *encoder);
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_EMBEDDING_EMBEDDING_FLOW_H_


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

#include "sling/nlp/embedding/embedding-model.h"

#include "sling/myelin/builder.h"
#include "sling/myelin/gradient.h"
#include "sling/util/random.h"

namespace sling {
namespace nlp {

using namespace myelin;

void MikolovFlow::Build() {
  BuildModel();
  BuildLayer0();
  BuildLayer1();
  BuildLayer0Back();
}

void MikolovFlow::BuildModel() {
  W0 = AddWeights("W0", DT_FLOAT, {inputs, dims});
  W1 = AddWeights("W1", DT_FLOAT, {outputs, dims});
}

void MikolovFlow::BuildLayer0() {
  layer0 = AddFunction("layer0");
  FlowBuilder tf(this, layer0);

  fv = tf.Var("features", DT_INT32, {1, in_features});
  hidden = tf.Name(tf.GatherAvg(W0, fv), "hidden");
}

void MikolovFlow::BuildLayer1() {
  layer1 = AddFunction("layer1");
  FlowBuilder tf(this, layer1);

  // Inputs.
  alpha = tf.Var("alpha", DT_FLOAT, {});
  label = tf.Var("label", DT_FLOAT, {1, 1});
  target = tf.Var("target", DT_INT32, {1, out_features});
  error = tf.Var("error", DT_FLOAT, {dims});
  l1_l0 = tf.Instance(layer0);
  auto *h = tf.Ref(l1_l0, hidden);

  // Output.
  bool single = out_features == 1;
  auto *embed = single ? tf.Gather(W1, target) : tf.GatherAvg(W1, target);
  auto *output = tf.Dot(embed, h, dims);

  // Loss.
  loss = tf.Name(tf.Sub(label, tf.Sigmoid(output)), "loss");
  loss->set_out();
  auto *eta = tf.Mul(loss, alpha);

  // Backprop layer 1.
  tf.AssignAdd(error, tf.Mul(embed, eta));
  tf.ScatterAdd(W1, target, tf.Mul(h, eta));
}

void MikolovFlow::BuildLayer0Back() {
  layer0b = AddFunction("layer0b");
  FlowBuilder tf(this, layer0b);

  l0b_l0 = tf.Instance(layer0);
  l0b_l1 = tf.Instance(layer1);
  tf.ScatterAdd(W0, tf.Ref(l0b_l0, fv), tf.Ref(l0b_l1, error));
}

void DualEncoderFlow::Build(const Transformations &library) {
  // Create left and right encoders.
  left.name = name + "/left";
  BuildEncoder(&left);
  right.name = name + "/right";
  BuildEncoder(&right);

  // Create gradient computations.
  left.backward = Gradient(this, left.forward, library);
  right.backward = Gradient(this, right.forward, library);

  // Create similarity computation.
  similarity = AddFunction(name + "/similarity");
  FlowBuilder tf(this, similarity);
  left_encodings = tf.Placeholder("left", DT_FLOAT, {batch_size, dims});
  right_encodings = tf.Placeholder("right", DT_FLOAT, {batch_size, dims});
  similarities = tf.MatMul(left_encodings, tf.Transpose(right_encodings));
  tf.Name(similarities, "similarities");
  gsimilarity = Gradient(this, similarity, library);
}

void DualEncoderFlow::BuildEncoder(Encoder *encoder) {
  encoder->forward = AddFunction(encoder->name);
  FlowBuilder tf(this, encoder->forward);
  encoder->embeddings =
      tf.Parameter("embeddings", DT_FLOAT, {encoder->dims, dims});
  encoder->features =
      tf.Placeholder("features", DT_INT32, {1, encoder->max_features});
  auto *sum = tf.GatherSum(encoder->embeddings, encoder->features);
  encoder->encoding = tf.Name(tf.Div(sum, tf.Norm(sum)), "encoding");
}

void Distribution::Shuffle() {
  // Shuffle elements (Fisher-Yates shuffle).
  int n = permutation_.size();
  Random rnd;
  for (int i = 0; i < n - 1; ++i) {
    int j = rnd.UniformInt(n - i);
    std::swap(permutation_[i], permutation_[i + j]);
  }

  // Convert weights to cumulative distribution.
  double sum = 0.0;
  for (const Element &e : permutation_) sum += e.probability;
  double acc = 0.0;
  for (Element &e : permutation_) {
    acc += e.probability / sum;
    e.probability = acc;
  }
}

int Distribution::Sample(float p) const {
  int n = permutation_.size();
  int low = 0;
  int high = n - 1;
  while (low < high) {
    int center = (low + high) / 2;
    if (permutation_[center].probability < p) {
      low = center + 1;
    } else {
      high = center;
    }
  }
  return permutation_[low].index;
}

}  // namespace nlp
}  // namespace sling


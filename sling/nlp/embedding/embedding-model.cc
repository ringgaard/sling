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
  W0->set_random();
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

  // Create similarity computation.
  similarity = AddFunction(name + "/similarity");
  FlowBuilder tf(this, similarity);
  left_encodings = tf.Placeholder("left", DT_FLOAT, {batch_size, dims});
  right_encodings = tf.Placeholder("right", DT_FLOAT, {batch_size, dims});
  similarities = tf.MatMul(left_encodings, tf.Transpose(right_encodings));
  tf.Name(similarities, "similarities");

  // Create gradient computations.
  left.backward = Gradient(this, left.forward, library);
  right.backward = Gradient(this, right.forward, library);
  gsimilarity = Gradient(this, similarity, library);
  gsimilarities = Var("gradients/" + name + "/similarity/d_similarities");
}

void DualEncoderFlow::BuildEncoder(Encoder *encoder) {
  encoder->forward = AddFunction(encoder->name);
  FlowBuilder tf(this, encoder->forward);
  encoder->embeddings =
      tf.Random(tf.Parameter("embeddings", DT_FLOAT, {encoder->dims, dims}));
  encoder->features =
      tf.Placeholder("features", DT_INT32, {1, encoder->max_features});
  auto *sum = tf.GatherSum(encoder->embeddings, encoder->features);
  auto *length = tf.Name(tf.Norm(sum), "length");
  encoder->encoding = tf.Name(tf.Mul(sum, tf.Reciprocal(length)), "encoding");
  encoder->encoding->set_ref();
}

DualEncoderBatch::DualEncoderBatch(const DualEncoderFlow &flow,
                                   const Network  &model,
                                   const CrossEntropyLoss &loss)
    : sim_(model.GetCell(flow.similarity)),
      gsim_(model.GetCell(flow.gsimilarity)),
      loss_(loss) {
  // Get cells for left end right encoders.
  const Cell *l = model.GetCell(flow.left.forward);
  const Cell *r = model.GetCell(flow.right.forward);
  const Cell *gl = model.GetCell(flow.left.backward);
  const Cell *gr = model.GetCell(flow.right.backward);

  // Allocate instances for all batch elements.
  elements_.reserve(flow.batch_size);
  for (int i = 0; i < flow.batch_size; ++i) {
    elements_.emplace_back(l, r, gl, gr);
  }

  // Get tensors.
  left_features_ = l->GetParameter(flow.left.features);
  right_features_ = r->GetParameter(flow.right.features);

  // Set up encoding references.
  auto *left_encoding = l->GetParameter(flow.left.encoding);
  auto *right_encoding = l->GetParameter(flow.right.encoding);
  auto *sim_left = sim_.cell()->GetParameter(flow.left_encodings);
  auto *sim_right = sim_.cell()->GetParameter(flow.right_encodings);
  for (int i = 0; i < flow.batch_size; ++i) {
    elements_[i].left.SetReference(left_encoding,
                                   sim_.Get<float>(sim_left, i));
    elements_[i].right.SetReference(right_encoding,
                                    sim_.Get<float>(sim_right, i));
  }

  // TODO: add gradient cells to optimizer.
}

float DualEncoderBatch::Compute() {
  // Compute left encodings.
  for (int i = 0; i < elements_.size(); ++i) {
    elements_[i].left.Compute();
  }

  // Compute right encodings.
  for (int i = 0; i < elements_.size(); ++i) {
    elements_[i].right.Compute();
  }

  // Compute similarity for all pairs in batch.
  sim_.Compute();

  // TODO: compute loss and propagate gradient to gsim.

  // TODO: propagate gradient through gsim.

  // TODO: propagate gradient through gleft and gright.

  return 0.0;
}

}  // namespace nlp
}  // namespace sling


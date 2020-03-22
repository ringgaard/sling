#include <iostream>
#include <string>

#include "sling/base/init.h"
#include "sling/myelin/compiler.h"
#include "sling/myelin/crf.h"
#include "sling/myelin/builder.h"

using namespace sling;
using namespace sling::myelin;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  int N = 15;
  int L = 3;

  Flow flow;
  FlowBuilder f(&flow, "f");
  auto *x = f.Placeholder("x", DT_FLOAT, {1, L})->set_out();
  auto *dx = f.Placeholder("dx", DT_FLOAT, {1, L})->set_out();
  f.Add(x, dx);

  CRF crf;
  crf.Build(&flow, x, dx);

  Compiler compiler;
  Network net;
  compiler.Compile(&flow, &net);

  crf.Initialize(net);

  TensorData transitions = net["crf/forward/transitions"];
  for (int i = 0; i < L + 2; ++i) {
    for (int j = 0; j < L + 2; ++j) {
      transitions.at<float>(i, j) = i == j ? 1.0 : 0.0;
    }
  }
  LOG(INFO) << "transitions: " << transitions.ToString();

  CRF::Learner learner(&crf);

  Channel input(x);
  input.resize(N);

  Channel dinput(dx);
  dinput.resize(N);

  std::vector<int> labels(N);
  for (int t = 0; t < N; ++t) {
    labels[t] = 2;
  }

  for (int t = 0; t < N; ++t) {
    TensorData x = input[t];
    for (int y = 0; y < L; ++y) {
      x.at<float>(0, y) = ((y + t) % 2) ? 10.0 : -10.0;
    }
  }
  LOG(INFO) << "input:\n" << input.ToString();

  float loss = learner.Learn(&input, labels, &dinput);

  LOG(INFO) << "loss: " << loss;
  return 0;
}


#include <iostream>
#include <string>
#include <random>

#include "sling/base/init.h"
#include "sling/myelin/compiler.h"
#include "sling/myelin/crf.h"
#include "sling/myelin/builder.h"

using namespace sling;
using namespace sling::myelin;

float nextval = 0.0;
float next() {
  nextval += 1.0;
  return nextval;
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  //std::mt19937_64 prng;
  //prng.seed(time(0));
  //std::normal_distribution<float> normal(0, 1.0);
  //std::uniform_real_distribution<float> uniform(-1.0, 1.0);

  int N = 5;
  int K = 3;

  Flow flow;
  FlowBuilder f(&flow, "f");
  auto *x = f.Placeholder("x", DT_FLOAT, {1, K})->set_out();
  auto *dx = f.Placeholder("dx", DT_FLOAT, {1, K})->set_out();
  f.Add(x, dx);

  CRF crf;
  crf.Build(&flow, x, dx);

  Compiler compiler;
  Network net;
  compiler.Compile(&flow, &net);
  net.InitModelParameters();

  crf.Initialize(net);

  TensorData transitions = net["crf/transitions"];
  for (int i = 0; i < K; ++i) {
    for (int j = 0; j < K; ++j) {
      transitions.at<float>(i, j) = next();
    }
  }
  LOG(INFO) << "transitions:\n" << transitions.ToString();

  CRF::Learner learner(&crf);

  Channel input(x);
  input.resize(N);

  Channel dinput(dx);
  dinput.resize(N);

  std::vector<int> labels(N);
  for (int t = 0; t < N; ++t) {
    labels[t] = (t + 1) % K;
    LOG(INFO) << "label " << t << ": " << labels[t];
  }

  for (int t = 0; t < N; ++t) {
    TensorData x = input[t];
    for (int y = 0; y < K; ++y) {
      x.at<float>(0, y) = next();
    }
  }
  LOG(INFO) << "input:\n" << input.ToString();

  float loss = learner.Learn(&input, labels, &dinput);

  LOG(INFO) << "dinput:\n" << dinput.ToString();

  LOG(INFO) << "loss: " << loss;

  LOG(INFO) << "Predictions:";
  CRF::Predictor predictor(&crf);
  std::vector<int> predictions;

  predictor.Predict(&input, &predictions);
  for (int l : predictions) LOG(INFO) << "label: " << l;

  return 0;
}


#include <iostream>
#include <string>

#include "sling/base/init.h"
#include "sling/base/flags.h"
#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/myelin/builder.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/flow.h"
#include "sling/myelin/gradient.h"
#include "sling/myelin/graph.h"
#include "sling/myelin/kernel/tensorflow.h"

DEFINE_bool(analyze, false, "Analyze flow");
DEFINE_bool(dump, false, "Dump flow");

using namespace sling;
using namespace sling::myelin;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up kernel library.
  Library library;
  RegisterTensorflowLibrary(&library);

  // Build flow.
  int vocab = 50000;
  int worddim = 64;
  Flow flow;
  Builder tf(&flow, "tagger");

  auto *word = tf.Placeholder("word", DT_INT32, {1, 1});
  auto *embedding = tf.Parameter("embedding", DT_FLOAT, {vocab, worddim});
  auto *features = tf.Gather(embedding, word);

  auto *hidden = tf.LSTMLayer(features, 128);
  auto *logits = tf.FFLayer(hidden, 43, true);

  Flow::Function *dtagger = Gradient(&flow, tf.func(), library);

  LOG(INFO) << "logits: " << logits->name;
  LOG(INFO) << "dtagger: " << dtagger->name;

  if (FLAGS_analyze) {
    flow.Analyze(library);
  }

  // Dump flow.
  if (FLAGS_dump) {
    std::cout << flow.ToString();
  }

  // Output DOT graph. The file can be converted to SVG using GraphWiz dot:
  // dot /tmp/model.dot -Tsvg > model.svg
  GraphOptions opts;
  FlowToDotGraphFile(flow, opts, "/tmp/postagger.dot");

  return 0;
}


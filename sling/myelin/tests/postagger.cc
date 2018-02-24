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

using namespace sling;
using namespace sling::myelin;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up kernel library.
  Library library;
  RegisterTensorflowLibrary(&library);

  // Build flow.
  Flow flow;
  Builder tf(&flow, "tagger");
  auto *input = tf.Var("input", DT_FLOAT, {1, 64});
  input->flags |= Flow::Variable::IN;

  auto *hidden = tf.LSTMLayer(input, 128);
  auto *logits = tf.FFLayer(hidden, 43, true);
  logits->flags |= Flow::Variable::OUT;

  /*Flow::Function *dtagger =*/ Gradient(&flow, tf.func(), library);

  //flow.Analyze(library);

  // Dump flow.
  std::cout << flow.ToString();

  // Output DOT graph. The file can be converted to SVG using GraphWiz dot:
  // dot /tmp/model.dot -Tsvg > model.svg
  GraphOptions opts;
  FlowToDotGraphFile(flow, opts, "/tmp/postagger.dot");

  return 0;
}


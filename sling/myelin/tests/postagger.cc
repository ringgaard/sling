#include <iostream>
#include <string>

#include "sling/base/init.h"
#include "sling/base/flags.h"
#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/myelin/builder.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/elf-linker.h"
#include "sling/myelin/flow.h"
#include "sling/myelin/gradient.h"
#include "sling/myelin/graph.h"
#include "sling/myelin/kernel/tensorflow.h"

DEFINE_bool(analyze, true, "Analyze flow");
DEFINE_bool(dump, false, "Dump flow");
DEFINE_bool(dump_cell, false, "Dump flow");

using namespace sling;
using namespace sling::myelin;

Flow::Function *BuildLoss(Flow *flow, int size) {
  Builder tf(flow, "loss");
  auto *logits = tf.Placeholder("logits", DT_FLOAT, {size});
  auto *target = tf.Placeholder("target", DT_INT32, {});
  auto *softmax = tf.Softmax(logits);
  tf.Name(tf.Op("DeltaCrossEntropy", {softmax}), "dlogits");
  tf.Name(tf.Neg(tf.Log(tf.Slice(softmax, target))), "loss");
  return tf.func();
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up kernel library.
  Library library;
  RegisterTensorflowLibrary(&library);

  jit::CPU::Enable(jit::AVX);
  jit::CPU::Enable(jit::AVX2);
  jit::CPU::Enable(jit::FMA3);

  // Build flow.
  int vocab = 50000;
  int worddim = 64;
  int lstmdim = 128;
  int tags = 43;
  Flow flow;
  Builder tf(&flow, "tagger");

  auto *word = tf.Placeholder("word", DT_INT32, {1, 1});
  auto *embedding = tf.Parameter("embedding", DT_FLOAT, {vocab, worddim});
  auto *features = tf.Gather(embedding, word);

  auto *hidden = tf.LSTMLayer(features, lstmdim);
  auto *logits = tf.FFLayer(hidden, tags, true);

  Flow::Function *dtagger = Gradient(&flow, tf.func(), library);
  //Flow::Function *loss = BuildLoss(&flow, tags);

  LOG(INFO) << "logits: " << logits->name;
  LOG(INFO) << "dtagger: " << dtagger->name;
  //LOG(INFO) << "loss: " << loss->name;

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

  // Compile network.
  Network network;
  ElfLinker linker;
  network.set_linker(&linker);
  network.Compile(flow, library);

  // Dump cell.
  for (Cell *cell : network.cells()) {
    if (FLAGS_dump_cell) {
      std::cout << cell->ToString();
    }
  }

  // Write object file with generated code.
  linker.Link();
  linker.Write("/tmp/postagger.o");

  return 0;
}


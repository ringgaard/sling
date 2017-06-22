#include <iostream>
#include <string>

#include "base/init.h"
#include "base/flags.h"
#include "base/logging.h"
#include "myelin/compute.h"
#include "myelin/flow.h"
#include "myelin/graph.h"
#include "myelin/profile.h"
#include "myelin/multi-process.h"
#include "myelin/kernel/tensorflow.h"

DEFINE_string(input, "local/tdozat.flow", "input file with flow model");
DEFINE_int32(repeat, 100, "Number of times test is repeated");
DEFINE_bool(dump_flow, false, "Dump analyzed flow to stdout");
DEFINE_bool(parallel, false, "Run matmuls in parallel");

using namespace sling;
using namespace sling::myelin;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up kernel library.
  Library library;
  RegisterTensorflowLibrary(&library);

  // Load model.
  Flow flow;
  flow.set_batch_size(1);
  CHECK(flow.Load(FLAGS_input));
  string prefix = "RNN0_2/RNN/while/time_step/rnn_step/LSTMCell/";
  flow.Var(prefix + "hidden_in/hidden_tm1:0")->data = nullptr;
  flow.Var(prefix + "hidden_in/cell_tm1:0")->data = nullptr;
  flow.Var(prefix + "inputs:0")->data = nullptr;
  flow.Var(prefix + "hidden_t/h_out:0")->out = true;
  flow.Var(prefix + "c_out:0")->out = true;

  if (FLAGS_parallel) {
    int t = 0;
    for (auto *matmul : flow.Find({"MatMul"})) {
      matmul->task = t++;
    }
  }

  GraphOptions rawopts;
  FlowToDotGraphFile(flow, rawopts, "/tmp/raw-tdozat.dot");

  // Analyze flow.
  flow.Analyze(library);
  DCHECK(flow.IsConsistent());

  if (FLAGS_dump_flow) {
    std::cout << flow.ToString();
  }

  // Compile model.
  Network network;
  MultiProcessorRuntime mprt;
  if (FLAGS_repeat > 0) network.set_profiling(true);
  if (FLAGS_parallel) network.set_runtime(&mprt);
  CHECK(network.Compile(flow, library));

  Cell *classifier = network.GetCell("classifier");

  // objdump -D -Mintel,x86-64 -bbinary -mi386 --no-show-raw-insn /tmp/tdozat.bin
  classifier->WriteCodeToFile("/tmp/tdozat.bin");

  // dot -Granksep=1.5 -Gnodesep=0.3 /tmp/tdozat.dot -Tsvg
  GraphOptions opts;
  FlowToDotGraphFile(flow, opts, "/tmp/tdozat.dot");

  // Test model.
  if (FLAGS_repeat > 0) {
    LOG(INFO) << "Profile model";
    Instance data(classifier);
    data.Clear();
    for (int i = 0; i < FLAGS_repeat; ++i) {
      data.Compute();
    }

    Profile profile(&data);
    std::cout << profile.ASCIIReport() << "\n";
  }

  return 0;
}

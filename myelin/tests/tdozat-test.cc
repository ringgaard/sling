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

DEFINE_string(model, "local/tdozat-step2.flow", "input file with flow model");
DEFINE_int32(repeat, 100, "Number of times test is repeated");
DEFINE_bool(dump_flow, false, "Dump analyzed flow to stdout");
DEFINE_bool(dump_cell, false, "Dump network cell to stdout");
DEFINE_bool(parallel, false, "Run matmuls in parallel");

using namespace sling;
using namespace sling::myelin;

void DummyDot(const TensorData &a,
              const TensorData &b,
              TensorData *result) {
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up kernel library.
  Library library;
  RegisterTensorflowLibrary(&library);
  library.Register("BatchMatMul", "DummyDot", DummyDot)
     .Input(0, DT_FLOAT, 3)
     .Input(1, DT_FLOAT, 3)
     .Output(0, DT_FLOAT, 3);

  // Load model.
  Flow flow;
  flow.set_batch_size(1);
  CHECK(flow.Load(FLAGS_model));

  string prefix = "RNN0_2/RNN/while/time_step/rnn_step/LSTMCell/";
  flow.Var(prefix + "hidden_in/hidden_tm1:0")->data = nullptr;
  flow.Var(prefix + "hidden_in/cell_tm1:0")->data = nullptr;
  flow.Var(prefix + "inputs:0")->data = nullptr;
  flow.Var(prefix + "hidden_t/h_out:0")->out = true;
  flow.Var(prefix + "c_out:0")->out = true;

  flow.Var("strided_slice_11:0")->name = "word1";
  flow.Var("strided_slice_12:0")->name = "word2";
  flow.Var("strided_slice_13:0")->name = "pos";

  flow.Var("recur_out_2:0")->in = true;
  flow.Var("recur_out_2:0")->data = nullptr;
  flow.Var("recur_out_2:0")->size = 0;

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

  // dot /tmp/tdozat.dot -Tsvg
  GraphOptions opts;
  FlowToDotGraphFile(flow, opts, "/tmp/tdozat.dot");

  // Compile model.
  Network network;
  MultiProcessorRuntime mprt;
  if (FLAGS_repeat > 0) network.set_profiling(true);
  if (FLAGS_parallel) network.set_runtime(&mprt);
  CHECK(network.Compile(flow, library));

  std::vector<string> cell_names = {"lookup", "lstmfw", "mlps"};
  for (const string &cell_name : cell_names) {
    Cell *cell = network.GetCell(cell_name);
    CHECK(cell != nullptr);
    if (FLAGS_dump_cell) {
      std::cout << cell->ToString();
    }

    // objdump -D -Mintel,x86-64 -bbinary -mi386 --no-show-raw-insn /tmp/xxx.bin
    cell->WriteCodeToFile("/tmp/" + cell_name + ".bin");

    // Test model.
    if (FLAGS_repeat > 0) {
      LOG(INFO) << "Profile " << cell_name;
      Instance data(cell);
      data.Clear();
      for (int i = 0; i < FLAGS_repeat; ++i) {
        data.Compute();
      }

      Profile profile(&data);
      std::cout << profile.ASCIIReport() << "\n";
    }
  }

  return 0;
}

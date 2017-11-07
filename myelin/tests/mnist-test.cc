#include <iostream>
#include <string>

#include "base/init.h"
#include "base/flags.h"
#include "base/logging.h"
#include "myelin/compute.h"
#include "myelin/flow.h"
#include "myelin/graph.h"
#include "myelin/profile.h"
#include "myelin/cuda/cuda-runtime.h"
#include "myelin/kernel/cuda.h"
#include "myelin/kernel/tensorflow.h"

DEFINE_string(input, "/tmp/mnist.flow", "input file with flow model");
DEFINE_int32(repeat, 100, "Number of times test is repeated");
DEFINE_bool(dump_flow, false, "Dump analyzed flow to stdout");
DEFINE_bool(dump_cell, false, "Dump network cell to stdout");
DEFINE_bool(gpu, false, "Run on GPU");

using namespace sling;
using namespace sling::myelin;

CUDARuntime cudart;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up kernel library.
  Library library;
  RegisterTensorflowLibrary(&library);
  if (FLAGS_gpu) RegisterCUDALibrary(&library);

  // Load model.
  Flow flow;
  flow.set_batch_size(1);
  CHECK(flow.Load(FLAGS_input));

  GraphOptions rawopts;
  FlowToDotGraphFile(flow, rawopts, "/tmp/mnist.dot");

  // Analyze flow.
  flow.Analyze(library);
  DCHECK(flow.IsConsistent());

  if (FLAGS_dump_flow) {
    std::cout << flow.ToString();
  }

  // Compile model.
  Network network;
  if (FLAGS_repeat > 0) network.set_profiling(true);
  if (FLAGS_gpu) {
    cudart.Connect();
    network.set_runtime(&cudart);
  }
  CHECK(network.Compile(flow, library));

  Cell *classifier = network.GetCell("classifier");
  if (FLAGS_dump_cell) {
    std::cout << classifier->ToString();
  }

  // objdump -D -Mintel,x86-64 -bbinary -mi386 --no-show-raw-insn /tmp/mnist.bin
  classifier->WriteCodeToFile("/tmp/mnist.bin");

  // dot -Granksep=1.5 -Gnodesep=0.3 /tmp/tdozat.dot -Tsvg
  GraphOptions opts;
  opts.max_value_size = 1;
  FlowToDotGraphFile(flow, opts, "/tmp/mnist.dot");

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


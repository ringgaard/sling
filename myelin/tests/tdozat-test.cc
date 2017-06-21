#include <iostream>
#include <string>

#include "base/init.h"
#include "base/logging.h"
#include "myelin/compute.h"
#include "myelin/flow.h"
#include "myelin/graph.h"
#include "myelin/profile.h"
#include "myelin/kernel/avx.h"
#include "myelin/kernel/generic.h"
#include "myelin/kernel/sse.h"
#include "myelin/kernel/arithmetic.h"

DEFINE_string(input, "local/tdozat.flow", "input file with flow model");

using namespace sling;
using namespace sling::myelin;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up kernel library.
  Library library;
  RegisterGenericTransformations(&library);
  RegisterArithmeticKernels(&library);
  RegisterAVXKernels(&library);
  RegisterSSEKernels(&library);
  RegisterGenericKernels(&library);

  // Load model.
  Flow flow;
  flow.set_batch_size(1);
  CHECK(flow.Load(FLAGS_input));
  string prefix = "RNN0_2/RNN/while/time_step/rnn_step/LSTMCell/";
  flow.Var(prefix + "hidden_in/hidden_tm1:0")->data = nullptr;
  flow.Var(prefix + "hidden_in/cell_tm1:0")->data = nullptr;
  flow.Var(prefix + "inputs:0")->data = nullptr;
  flow.Var(prefix + "input_gate/Linear/Add:0")->out = true;
  flow.Var(prefix + "input_gate/Linear/Add:0")->name = prefix + "control_out:0";

  // Analyze flow.
  flow.Analyze(library);
  DCHECK(flow.IsConsistent());

#if 0
  std::cout << flow.ToString();
#endif

  Network network;
  network.set_profiling(true);
  CHECK(network.Compile(flow, library));

  Cell *classifier = network.GetCell("classifier");

  // objdump -D -Mintel,x86-64 -bbinary -mi386 --no-show-raw-insn /tmp/tdozat.bin
  classifier->WriteCodeToFile("/tmp/tdozat.bin");

  // dot -Granksep=1.5 -Gnodesep=0.3 -Grankdir=BT /tmp/tdozat.dot -Tsvg
  GraphOptions opts;
  FlowToDotGraphFile(flow, opts, "/tmp/tdozat.dot");

  // Run instance
  Instance data(classifier);
  for (int i = 0; i < 1000000; ++i) {
    data.Compute();
  }

  Profile profile(&data);
  std::cout << profile.ASCIIReport() << "\n";

  return 0;
}

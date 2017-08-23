#include <iostream>
#include <string>

#include "base/init.h"
#include "base/flags.h"
#include "base/logging.h"
#include "myelin/compute.h"
#include "myelin/flow.h"
#include "myelin/graph.h"
#include "myelin/profile.h"
#include "myelin/kernel/tensorflow.h"

DEFINE_string(input, "local/sempar/sempar.flow", "input file with flow model");
DEFINE_bool(dump_flow, false, "Dump analyzed flow to stdout");

using namespace sling;
using namespace sling::myelin;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up kernel library.
  Library library;
  RegisterTensorflowLibrary(&library);

  // Load model.
  Flow flow;
  CHECK(flow.Load(FLAGS_input));

  GraphOptions rawopts;
  FlowToDotGraphFile(flow, rawopts, "/tmp/raw-sempar.dot");

  // Analyze flow.
  flow.Analyze(library);
  DCHECK(flow.IsConsistent());

  FlowToDotGraphFile(flow, rawopts, "/tmp/sempar.dot");

  if (FLAGS_dump_flow) {
    std::cout << flow.ToString();
  }
}


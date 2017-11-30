#include <iostream>
#include <string>

#include "sling/base/init.h"
#include "sling/base/flags.h"
#include "sling/base/logging.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/flow.h"
#include "sling/myelin/graph.h"
#include "sling/myelin/profile.h"
#include "sling/myelin/kernel/dragnn.h"
#include "sling/myelin/kernel/tensorflow.h"

DEFINE_string(input, "local/sempar/sempar.flow", "input file with flow model");
DEFINE_bool(dump_flow, false, "Dump analyzed flow to stdout");
DEFINE_bool(dump_raw_flow, false, "Dump input flow to stdout");

using namespace sling;
using namespace sling::myelin;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up kernel library.
  Library library;
  RegisterTensorflowLibrary(&library);
  RegisterDragnnLibrary(&library);

  // Load model.
  Flow flow;
  CHECK(flow.Load(FLAGS_input));

  if (FLAGS_dump_raw_flow) {
    std::cout << flow.ToString();
  }

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


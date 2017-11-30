#include <iostream>
#include <string>

#include "sling/base/clock.h"
#include "sling/base/init.h"
#include "sling/base/flags.h"
#include "sling/base/logging.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/flow.h"
#include "sling/myelin/graph.h"
#include "sling/myelin/profile.h"
#include "sling/myelin/builder.h"
#include "sling/myelin/cuda/cuda-runtime.h"
#include "sling/myelin/kernel/cuda.h"
#include "sling/myelin/kernel/tensorflow.h"

DEFINE_bool(dump_flow, false, "Dump analyzed flow to stdout");
DEFINE_bool(profile, false, "Profile computation");
DEFINE_int32(repeat, 100, "Number of times test is repeated");
DEFINE_int32(size, 256, "Vector size");

using namespace sling;
using namespace sling::myelin;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up kernel library.
  Library library;
  RegisterTensorflowLibrary(&library);
  RegisterCUDALibrary(&library);

  // Set up CUDA runtime.
  CUDARuntime cudart;
  cudart.Connect();
  LOG(INFO) << cudart.Description();

  // Set up workflow.
  Flow flow;
  Builder tf(&flow, "test");
  int size = FLAGS_size;
  auto *a = tf.Var("a", DT_FLOAT, {size});
  auto *b = tf.Var("b", DT_FLOAT, {size});
  auto *c = tf.Tanh(tf.Mul(tf.Add(a, b), tf.Sub(a, b)));
  c->out = true;

  // Analyze flow.
  flow.Analyze(library);
  DCHECK(flow.IsConsistent());

  GraphOptions rawopts;
  FlowToDotGraphFile(flow, rawopts, "/tmp/cuda.dot");

  if (FLAGS_dump_flow) {
    std::cout << flow.ToString();
  }

  // Compile model.
  Network network;
  network.set_runtime(&cudart);
  if (FLAGS_profile) network.set_profiling(true);
  CHECK(network.Compile(flow, library));

  Cell *cell = network.GetCell("test");

  // objdump -D -Mintel,x86-64 -bbinary -mi386 --no-show-raw-insn /tmp/cuda.bin
  cell->WriteCodeToFile("/tmp/cuda.bin");

  // Test model.
  if (FLAGS_repeat > 0) {
    LOG(INFO) << "Profile model";
    Instance data(cell);

    data.Clear();
    TensorData a = data["a"];
    TensorData b = data["b"];
    for (int i = 0; i < size; i++) {
      a.at<float>(i) = i * 2;
      b.at<float>(i) = i * 2 + 1;
    }

    Clock clock;
    clock.start();
    for (int i = 0; i < FLAGS_repeat; ++i) {
      data.Compute();
    }
    clock.stop();
    LOG(INFO) << clock.cycles() / FLAGS_repeat << " cycles, "
              << clock.us() / FLAGS_repeat << " us";

    //LOG(INFO) << "Data:\n" << data.ToString();

    if (FLAGS_profile) {
      Profile profile(&data);
      std::cout << profile.ASCIIReport() << "\n";
    }
  }

  return 0;
}

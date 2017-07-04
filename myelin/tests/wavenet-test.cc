#include <iostream>
#include <set>
#include <string>
#include <unordered_map>

#include "base/init.h"
#include "base/logging.h"
#include "myelin/compute.h"
#include "myelin/flow.h"
#include "myelin/graph.h"
#include "myelin/profile.h"
#include "myelin/kernel/tensorflow.h"
#include "myelin/kernel/wavenet.h"
#include "myelin/macro-assembler.h"

#define __ masm->

DEFINE_string(input, "local/wavenet.flow", "input file with flow model");

using namespace sling;
using namespace sling::jit;
using namespace sling::myelin;

class ZigZag16 : public Kernel {
 public:
  string Name() override { return "ZigZag16"; }
  string Operation() override { return "ZigZag16"; }

  bool Supports(Step *step) override {
    return true;
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    Tensor *x = step->input(0);
    Tensor *y = step->output(0);

    Register input = masm->rr().alloc();
    Register output = masm->rr().alloc();
    YMMRegister a0 = masm->mm().allocy();
    YMMRegister a1 = masm->mm().allocy();
    YMMRegister a2 = masm->mm().allocy();
    YMMRegister b0 = masm->mm().allocy();
    YMMRegister b1 = masm->mm().allocy();
    YMMRegister b2 = masm->mm().allocy();

    __ LoadTensorAddress(input, x);
    __ LoadTensorAddress(output, y);

    __ vmovaps(a0, Operand(input));      // [0 1 2 3 | 4 5 6 7]
    __ vmovaps(b0, Operand(input, 32));  // [8 9 A B | C D E F]

    __ vpermq(a1, a0, 0x4E);        // [4 5 6 7 | 0 1 2 3]   01001110b = 0x4E
    __ vpermilps(a0, a0, 0xD8);     // [0 2 1 3 | 4 6 5 7]   11011000b = 0xD8
    __ vpermilps(a1, a1, 0x8D);     // [5 7 4 6 | 1 3 0 2]   10001101b = 0x8D
    __ vblendps(a0, a0, a1, 0x3C);  // [0 2 4 6 | 1 3 5 7]   00111100b = 0x3C
    __ vpermq(a1, a0, 0x4E);        // [1 3 5 7 | 0 2 4 6]

    __ vpermq(b1, b0, 0x4E);        // [C D E F | 8 9 A B]
    __ vpermilps(b0, b0, 0xD8);     // [8 A 9 B | C E D F]
    __ vpermilps(b1, b1, 0x8D);     // [D F C E | 9 B 8 A]
    __ vblendps(b0, b0, b1, 0x3C);  // [8 A C E | 9 B D F]
    __ vpermq(b1, b0, 0x4E);        // [9 B D F | 8 A C E]

    __ vblendps(a2, a0, b1, 0xF0);  // [0 2 4 6 | 8 A C E]
    __ vblendps(b2, a1, b0, 0xF0);  // [1 3 5 7 | 9 B D F]

    __ vmovaps(Operand(output), a2);
    __ vmovaps(Operand(output, 32), b2);
  }
};


void ZigZagTest() {
  Library library;
  library.Register(new ZigZag16());

  Flow flow;
  auto *x = flow.AddVariable("x", DT_FLOAT, {16});
  auto *y = flow.AddVariable("y", DT_FLOAT, {16});

  auto *func = flow.AddFunction("test");
  flow.AddOperation(func, "zigzag", "ZigZag16", {x}, {y});

  Network network;
  CHECK(network.Compile(flow, library));
  Cell *cell = network.GetCell("test");
  cell->WriteCodeToFile("/tmp/zigzag.bin");

  Instance data(cell);
  auto xval = data[cell->GetParameter("x")];
  auto yval = data[cell->GetParameter("y")];
  for (int i = 0; i < 16; ++i) xval.at<float>(i) = i;
  data.Compute();
  for (int i = 0; i < 16; ++i) {
    LOG(INFO) << xval.at<float>(i) << " " << yval.at<float>(i);
  }
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Select CPU features.
  //jit::CPU::Disable(jit::SSE2);
  //jit::CPU::Disable(jit::SSE3);
  //jit::CPU::Disable(jit::SSE4_1);
  //jit::CPU::Disable(jit::AVX);
  //jit::CPU::Disable(jit::AVX2);
  //jit::CPU::Disable(jit::FMA3);

  //ZigZagTest();

  // Set up kernel library.
  Library library;
  RegisterTensorflowLibrary(&library);
  RegisterWaveNetLibrary(&library);

  // Load model.
  Flow flow;
  CHECK(flow.Load(FLAGS_input));

  // Set input and output names.
  flow.Var("input_log_f0:0")->name = "input_log_f0";
  flow.Var("input_linguistic:0")->name = "input_linguistic";
  flow.Var("output_waveform:0")->name = "output_waveform";

  // Add seed to random generator.
  auto *seed = flow.AddVariable("input_seed", DT_INT64, {});
  flow.Op("random_uniform/RandomUniform")->AddInput(seed);

#if 0
  GraphOptions ropts;
  FlowToDotGraphFile(flow, ropts, "/tmp/raw-wavenet.dot");
#endif

  // Analyze flow.
  flow.Analyze(library);
  DCHECK(flow.IsConsistent());

  LOG(INFO) << flow.ops().size() << " ops";
  LOG(INFO) << flow.vars().size() << " vars";

#if 0
  std::cout << flow.ToString();
  std::cout.flush();
#endif

  Network network;
  network.set_dynamic_allocation(true);
  network.set_profiling(true);
  CHECK(network.Compile(flow, library));

  // Inspect with: objdump -D -Mintel,x86-64 -b binary -m i386 /tmp/distil.bin
  Cell *distil = network.GetCell("distil");

  distil->WriteCodeToFile("/tmp/distil.bin");

  int noops = 0;
  for (auto *step : distil->steps()) {
    if (step->noop()) noops++;
  }
  LOG(INFO) << noops << " noops";

  int consts = network.constants().size();
  int params = 0;
  int shared = 0;
  for (auto *t : network.parameters()) {
    if (t->shared() != nullptr) {
      shared++;
    } else {
      params++;
    }
  }
  LOG(INFO) << consts << " constants";
  LOG(INFO) << params << " parameters";
  LOG(INFO) << shared << " shared";
  LOG(INFO) << distil->instance_size() << " bytes instance";

#if 0
  std::cout << distil->ToString();
  std::cout.flush();
#endif

  // Convert to DOT graph.
  // To convert to SVG use:
  // dot -Gnslimit=10 /tmp/wavenet.dot -Tsvg > /tmp/wavenet.svg
  GraphOptions options;
  //options.edge_thickness_scalar = 0.3;

#if 0
  GraphNodeOptions noop_options = options.ops;
  noop_options.fillcolor = "#BDDBDB";
  noop_options.color = "#849999";
  for (auto *step : distil->steps()) {
    if (step->noop()) {
      options.custom_ops[step->name()] = noop_options;
    }
  }
#endif

#if 1

  GraphNodeOptions shared_options = options.ops;
  shared_options.fillcolor = "#BDDBDB";
  shared_options.color = "#849999";
  for (auto *step : distil->steps()) {
    if (step->outdegree() > 0 && step->output(0)->shared() != nullptr) {
      options.custom_ops[step->name()] = shared_options;
    }
  }
#endif

  FlowToDotGraphFile(flow, options, "/tmp/wavenet.dot");

#if 0
  size_t size = 0;
  size_t elems = 0;
  for (Tensor *var : network.parameters()) {
    size += var->size();
    elems += var->elements();
  }
  LOG(INFO) << size << " bytes total";
  LOG(INFO) << elems << " elements total";
#endif

#if 0
  std::cout << "Name\tKernel\tComplexity\n";
  for (Step *step : distil->steps()) {
    std::cout << step->name() << "\t";
    std::cout << step->kernel()->Name() << "\t";
    std::cout << Profile::Complexity(step) << "\n";
  }
#endif

#if 0
  for (Step *step : distil->steps()) {
    if (step->type() == "Conv1D") {
      std::cout << "Conv1D    " << step->complexity() << " "
                << step->input(0)->shape().ToString() << " "
                << step->input(2)->shape().ToString() << " "
                << step->output(0)->shape().ToString() << " "
                << step->name() << " "
                << "\n";
    }
    if (step->type() == "Conv1DAdd") {
      std::cout << "Conv1DAdd " << step->complexity() << " "
                << step->input(0)->shape().ToString() << " "
                << step->input(2)->shape().ToString() << " "
                << step->input(3)->shape().ToString() << " "
                << step->output(0)->shape().ToString() << " "
                << step->name() << " "
                << "\n";
    }
  }
#endif

#if 1
  // Run instance
  Instance data(distil);
  for (int i = 0; i < 10; ++i) {
    data.Compute();
  }

  Profile profile(&data);
  std::cout << profile.ASCIIReport() << "\n";
#endif

  return 0;
}

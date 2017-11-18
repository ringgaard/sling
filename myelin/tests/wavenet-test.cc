#include <iostream>
#include <set>
#include <string>
#include <unordered_map>

#include "base/flags.h"
#include "base/init.h"
#include "base/logging.h"
#include "file/file.h"
#include "myelin/compute.h"
#include "myelin/flow.h"
#include "myelin/graph.h"
#include "myelin/profile.h"
#include "myelin/kernel/tensorflow.h"
#include "myelin/kernel/wavenet.h"
#include "myelin/macro-assembler.h"

#define __ masm->

DEFINE_string(input, "local/wavenet.flow", "input file with flow model");
DEFINE_bool(zigzag, false, "Test ZigZag kernels");
DEFINE_bool(dump_flow, false, "Dump flow");
DEFINE_bool(dump_cell, false, "Dump cell");
DEFINE_bool(profile, false, "Profile benchmark");
DEFINE_bool(opstat, false, "Output operation statistics");
DEFINE_bool(custom_deconv, false, "Custom deconvolution kernel");
DEFINE_int32(repeat, 1, "Number of times benchmark is repeated");

using namespace sling;
using namespace sling::jit;
using namespace sling::myelin;

class ZigZagAVX : public Kernel {
 public:
  string Name() override { return "ZigZagAVX"; }
  string Operation() override { return "ZigZag"; }

  bool Supports(Step *step) override {
    if (step->input(0)->elements() % 16 != 0) return false;
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

    __ vperm2f128(a1, a0, a0, 1);   // [4 5 6 7 | 0 1 2 3]   01001110b = 0x4E
    __ vpermilps(a0, a0, 0xD8);     // [0 2 1 3 | 4 6 5 7]   11011000b = 0xD8
    __ vpermilps(a1, a1, 0x8D);     // [5 7 4 6 | 1 3 0 2]   10001101b = 0x8D
    __ vblendps(a0, a0, a1, 0x3C);  // [0 2 4 6 | 1 3 5 7]   00111100b = 0x3C

    __ vperm2f128(b1, b0, b0, 1);   // [C D E F | 8 9 A B]
    __ vpermilps(b0, b0, 0xD8);     // [8 A 9 B | C E D F]
    __ vpermilps(b1, b1, 0x8D);     // [D F C E | 9 B 8 A]
    __ vblendps(b0, b0, b1, 0x3C);  // [8 A C E | 9 B D F]

    __ vperm2f128(a2, a0, b0, 0x20);   // [0 2 4 6 | 8 A C E]
    __ vperm2f128(b2, a0, b0, 0x31);   // [1 3 5 7 | 9 B D F]

    __ vmovaps(Operand(output), a2);
    __ vmovaps(Operand(output, 32), b2);
  }
};

class ZigZagSSE : public Kernel {
 public:
  string Name() override { return "ZigZagSSE"; }
  string Operation() override { return "ZigZag"; }

  bool Supports(Step *step) override {
    if (step->input(0)->elements() % 8 != 0) return false;
    return true;
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    Tensor *x = step->input(0);
    Tensor *y = step->output(0);

    Register input = masm->rr().alloc();
    Register output = masm->rr().alloc();
    XMMRegister a0 = masm->mm().allocx();
    XMMRegister a1 = masm->mm().allocx();
    XMMRegister b0 = masm->mm().allocx();
    XMMRegister b1 = masm->mm().allocx();

    __ LoadTensorAddress(input, x);
    __ LoadTensorAddress(output, y);

    __ movaps(a0, Operand(input));      // [0 1 2 3]
    __ movaps(b0, Operand(input, 16));  // [4 5 6 7]

    __ movaps(a1, a0);                  // [0 1 2 3]
    __ shufps(a1, b0, 0x88);            // [0 2 4 6]

    __ movaps(b1, a0);                  // [0 1 2 3]
    __ shufps(b1, b0, 0xDD);            // [1 3 5 7]

    __ movaps(Operand(output), a1);
    __ movaps(Operand(output, 16), b1);
  }
};

void ZigZagTest() {
  Library library;
  library.Register(new ZigZagSSE());
  library.Register(new ZigZagAVX());

  if (CPU::Enabled(AVX)) {
    Flow flow;
    auto *x = flow.AddVariable("x", DT_FLOAT, {16});
    auto *y = flow.AddVariable("y", DT_FLOAT, {16});
    auto *func = flow.AddFunction("test");
    flow.AddOperation(func, "zigzag", "ZigZag", {x}, {y});

    Network network;
    CHECK(network.Compile(flow, library));
    Cell *cell = network.GetCell("test");
    cell->WriteCodeToFile("/tmp/zigzag256.bin");

    Instance data(cell);
    auto xval = data[cell->GetParameter("x")];
    auto yval = data[cell->GetParameter("y")];
    for (int i = 0; i < 16; ++i) xval.at<float>(i) = i;
    data.Compute();
    for (int i = 0; i < 16; ++i) {
      LOG(INFO) << xval.at<float>(i) << " " << yval.at<float>(i);
    }
  }

  if (CPU::Enabled(SSE)) {
    Flow flow;
    auto *x = flow.AddVariable("x", DT_FLOAT, {8});
    auto *y = flow.AddVariable("y", DT_FLOAT, {8});
    auto *func = flow.AddFunction("test");
    flow.AddOperation(func, "zigzag", "ZigZag", {x}, {y});

    Network network;
    CHECK(network.Compile(flow, library));
    Cell *cell = network.GetCell("test");
    cell->WriteCodeToFile("/tmp/zigzag128.bin");

    Instance data(cell);
    auto xval = data[cell->GetParameter("x")];
    auto yval = data[cell->GetParameter("y")];
    for (int i = 0; i < 8; ++i) xval.at<float>(i) = i;
    data.Compute();
    for (int i = 0; i < 8; ++i) {
      LOG(INFO) << xval.at<float>(i) << " " << yval.at<float>(i);
    }
  }
}

void Deconv1D(const TensorData &input,
              const TensorData &filter,
              TensorData *output) {
  LOG(INFO) << "Deconv1D:"
            << " input: " << input.shape().ToString()
            << " filter: " << filter.shape().ToString()
            << " output: " << output->shape().ToString();
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Test ZigZag kernels.
  if (FLAGS_zigzag) ZigZagTest();

  // Set up kernel library.
  Library library;
  RegisterTensorflowLibrary(&library);
  RegisterWaveNetLibrary(&library);
  if (FLAGS_custom_deconv) {
    library.Register("Deconv1D", "CostumDeconv1D", Deconv1D);
  }

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

  // Analyze flow.
  flow.Analyze(library);
  DCHECK(flow.IsConsistent());

  // Dump flow.
  if (FLAGS_dump_flow) {
    std::cout << flow.ToString();
    std::cout.flush();
  }

  // Compile network.
  Network network;
  network.set_dynamic_allocation(true);
  if (FLAGS_profile) network.set_profiling(true);
  CHECK(network.Compile(flow, library));

  // Inspect with: objdump -D -Mintel,x86-64 -b binary -m i386 /tmp/distil.bin
  Cell *distil = network.GetCell("distil");

  // Write generated code.
  distil->WriteCodeToFile("/tmp/distil.bin");

  // Dump cell.
  if (FLAGS_dump_cell) {
    std::cout << distil->ToString();
    std::cout.flush();
  }

  // Output operation statistics.
  if (FLAGS_opstat) {
    LOG(INFO) << flow.ops().size() << " ops";
    LOG(INFO) << flow.vars().size() << " vars";

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
  }

  // Output data instance diagram.
  DataProfile dprof(distil);
  File::WriteContents("/tmp/distil-data.svg", dprof.AsSVG());

  // Convert to DOT graph.
  // To convert to SVG use:
  // dot -Gnslimit=10 /tmp/wavenet.dot -Tsvg > /tmp/wavenet.svg
  GraphOptions options;
  GraphNodeOptions noop = options.ops;
  noop.fillcolor = "#BDDBDB";
  noop.color = "#849999";
  for (auto *step : distil->steps()) {
    if (step->noop()) {
      options.custom_ops[step->name()] = noop;
    }
  }
  FlowToDotGraphFile(flow, options, "/tmp/wavenet.dot");

  // Run instance
  Instance data(distil);
  for (int i = 0; i < FLAGS_repeat; ++i) {
    data.Compute();
  }

  if (FLAGS_profile) {
    Profile profile(&data, Profile::COMPLEXITY);
    std::cout << profile.ASCIIReport() << "\n";
  }

  return 0;
}


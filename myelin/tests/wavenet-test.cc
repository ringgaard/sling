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

DEFINE_string(input, "local/wavenet.flow", "input file with flow model");

using namespace sling;
using namespace sling::myelin;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Select CPU features.
  //jit::CPU::Disable(jit::SSE2);
  //jit::CPU::Disable(jit::SSE3);
  //jit::CPU::Disable(jit::SSE4_1);
  //jit::CPU::Disable(jit::AVX);
  //jit::CPU::Disable(jit::AVX2);
  //jit::CPU::Disable(jit::FMA3);

  // Set up kernel library.
  Library library;
  RegisterTensorflowLibrary(&library);
  RegisterWaveNetLibrary(&library);

#if 0
  // Load model.
  Flow mainflow;
  CHECK(mainflow.Load(FLAGS_input));

  // Set input and output names.
  mainflow.Var("input_log_f0:0")->name = "input_log_f0";
  mainflow.Var("input_linguistic:0")->name = "input_linguistic";
  mainflow.Var("output_waveform:0")->name = "output_waveform";

  // Add seed to random generator.
  auto *seed = mainflow.AddVariable("input_seed", DT_INT64, {});
  mainflow.Op("random_uniform/RandomUniform")->AddInput(seed);

  // Create sub-model.
  Flow flow;
  mainflow.Extract("distil", {}, {mainflow.Var("sub_1")}, &flow);
  DCHECK(flow.IsConsistent());

#else
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

  //GraphOptions opts;
  //FlowToDotGraphFile(flow, opts, "/tmp/raw-wavenet.dot");

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

#if 0
  std::unordered_map<string, int> nodes;
  for (auto *op : flow.ops()) {
    nodes[op->type] += 1;
  }
  for (auto &it : nodes) {
    std::cout << it.second << "\t" << it.first << "\n";
  }
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

#include <iostream>
#include <string>

#include "sling/base/init.h"
#include "sling/base/flags.h"
#include "sling/base/logging.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/flow.h"
#include "sling/myelin/graph.h"
#include "sling/myelin/profile.h"
#include "sling/myelin/multi-process.h"
#include "sling/myelin/kernel/tensorflow.h"

DEFINE_string(model, "local/tdozat-step4.flow", "input file with flow model");
DEFINE_int32(repeat, 100, "Number of times test is repeated");
DEFINE_bool(dump_raw_flow, false, "Dump raw flow to stdout");
DEFINE_bool(dump_flow, false, "Dump analyzed flow to stdout");
DEFINE_bool(dump_cell, false, "Dump network cell to stdout");
DEFINE_bool(parallel, false, "Run matmuls in parallel");

using namespace sling;
using namespace sling::myelin;

float filler(int i) {
  return (i % 6) - 3;
}

void makeref(Flow::Variable *var, Flow::Connector *cnx) {
  var->ref = true;
  var->flags |= Flow::Variable::IN | Flow::Variable::OUT;
  cnx->AddLink(var);
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up kernel library.
  Library library;
  RegisterTensorflowLibrary(&library);

  // Load model.
  Flow flow;
  flow.set_batch_size(1);
  CHECK(flow.Load(FLAGS_model));

  if (FLAGS_dump_raw_flow) {
    std::cout << flow.ToString();
    std::cout.flush();
  }

  Flow::Connector *fw_lstm_h = flow.AddConnector("fw_lstm_h");
  Flow::Connector *fw_lstm_c = flow.AddConnector("fw_lstm_c");

  string lstmfw_prefix = "RNN0_2/BiRNN_FW/BiRNN_FW/while/rnn_step/LSTMCell/";
  string lstmbw_prefix = "RNN0_2/BiRNN_BW/BiRNN_BW/while/rnn_step/LSTMCell/";

  auto *fw_c_in = flow.Var(lstmfw_prefix + "hidden_in/cell_tm1:0");
  auto *fw_h_in = flow.Var(lstmfw_prefix + "hidden_in/hidden_tm1:0");
  auto *fw_c_out = flow.Var(lstmfw_prefix + "c_out:0");
  auto *fw_h_out = flow.Var(lstmfw_prefix + "hidden_t/h_out:0");

  //auto *fw_input = flow.Var(lstmfw_prefix + "inputs:0");

  makeref(fw_h_in, fw_lstm_h);
  makeref(fw_h_out, fw_lstm_h);
  makeref(fw_c_in, fw_lstm_c);
  makeref(fw_c_out, fw_lstm_c);

  Flow::Connector *bw_lstm_h = flow.AddConnector("bw_lstm_h");
  Flow::Connector *bw_lstm_c = flow.AddConnector("bw_lstm_c");

  auto *bw_c_in = flow.Var(lstmbw_prefix + "hidden_in/cell_tm1:0");
  auto *bw_h_in = flow.Var(lstmbw_prefix + "hidden_in/hidden_tm1:0");
  auto *bw_c_out = flow.Var(lstmbw_prefix + "c_out:0");
  auto *bw_h_out = flow.Var(lstmbw_prefix + "hidden_t/h_out:0");

  makeref(fw_h_in, bw_lstm_h);
  makeref(fw_h_out, bw_lstm_h);
  makeref(fw_c_in, bw_lstm_c);
  makeref(fw_c_out, bw_lstm_c);

  flow.Var("lookup_2/strided_slice:0")->name = "word1";
  flow.Var("lookup_2/strided_slice_1:0")->name = "word2";
  flow.Var("lookup_2/strided_slice_2:0")->name = "pos";

  flow.Var("recur_nob_A_2:0")->flags |= Flow::Variable::IN;
  flow.Var("recur_nob_B_2:0")->flags |= Flow::Variable::IN;

#if 0
  // Patch Tanh.
  auto *tanh_2 = flow.Op(lstmfw_prefix + "Tanh_2");
  auto *tanh_5 = flow.Op(lstmfw_prefix + "Tanh_5");
  auto *mul_2 = flow.Op(lstmfw_prefix + "Mul_2");
  mul_2->ReplaceInput(tanh_5->outputs[0], tanh_2->outputs[0]);
  flow.DeleteVariable(tanh_5->outputs[0]);
  flow.RemoveOperation(tanh_5);
#endif

  if (FLAGS_parallel) {
    int t = 0;
    for (auto *matmul : flow.Find({"MatMul"})) {
      matmul->task = t++;
    }
  }

  GraphOptions rawopts;
  FlowToDotGraphFile(flow, rawopts, "/tmp/raw-tdozat.dot");

#if 0
  // Extract hidden and control computation.
  Flow hcflow;
  std::vector<Flow::Variable *> hcinputs;
  for (auto *matmul : flow.Find("MatMul")) {
    LOG(INFO) << "MATMUL " << matmul->name;
    hcinputs.push_back(matmul->outputs[0]);
  }
  flow.Extract("hc", hcinputs, {fw_h_out, fw_c_out}, &hcflow);
  std::cout << hcflow.ToString();
  std::cout.flush();

  hcflow.Analyze(library);
  DCHECK(hcflow.IsConsistent());

  Network hc;
  CHECK(hc.Compile(hcflow, library));

  std::cout << hc.GetCell("hc")->ToString();
  std::cout.flush();

  hc.GetCell("hc")->WriteCodeToFile("/tmp/hc.bin");

  return 0;
#endif

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

#if 0
  // Test tanh problem.
  Cell *fw_lstm = network.GetCell("fw_lstm");
  Instance data(fw_lstm);
  Channel control(network.GetConnector("fw_lstm_c"));
  control.resize(2);
  data.Set(fw_lstm->GetParameter(fw_c_in->name), &control, 0);
  data.Set(fw_lstm->GetParameter(fw_c_out->name), &control, 1);
  Channel hidden(network.GetConnector("fw_lstm_h"));
  hidden.resize(2);
  data.Set(fw_lstm->GetParameter(fw_h_in->name), &hidden, 0);
  data.Set(fw_lstm->GetParameter(fw_h_out->name), &hidden, 1);

  float *word_emb = data.Get<float>(fw_lstm->GetParameter(fw_input->name));
  for (int i = 0; i < 80; ++i) word_emb[i] = filler(i);
  float *c0 = reinterpret_cast<float *>(control.at(0));
  float *h0 = reinterpret_cast<float *>(hidden.at(0));
  for (int i = 0; i < 128; ++i) {
    c0[i] = filler(i);
    h0[i] = filler(i + 1);
  }

  data.Compute();
  std::cout << data.ToString();
#endif

  // Test model.
  std::vector<string> cell_names = {"lookup", "fw_lstm", "bw_lstm", "mlps"};
  for (const string &cell_name : cell_names) {
    Cell *cell = network.GetCell(cell_name);
    if (FLAGS_dump_cell) {
      std::cout << cell->ToString();
    }

    // objdump -D -Mintel,x86-64 -bbinary -mi386 --no-show-raw-insn /tmp/xxx.bin
    cell->WriteCodeToFile("/tmp/" + cell_name + ".bin");

    if (FLAGS_repeat > 0) {
      LOG(INFO) << "Profile " << cell_name;
      Instance data(cell);
      if (cell_name == "fw_lstm") {
        Channel control(network.GetParameter(fw_c_in->name));
        control.resize(2);
        data.Set(cell->GetParameter(fw_c_in->name), &control, 0);
        data.Set(cell->GetParameter(fw_c_out->name), &control, 1);
        Channel hidden(network.GetParameter(fw_h_in->name));
        hidden.resize(2);
        data.Set(cell->GetParameter(fw_h_in->name), &hidden, 0);
        data.Set(cell->GetParameter(fw_h_out->name), &hidden, 1);
      } else if (cell_name == "bw_lstm") {
        Channel control(network.GetParameter(bw_c_in->name));
        control.resize(2);
        data.Set(cell->GetParameter(bw_c_in->name), &control, 0);
        data.Set(cell->GetParameter(bw_c_out->name), &control, 1);
        Channel hidden(network.GetParameter(bw_h_in->name));
        hidden.resize(2);
        data.Set(cell->GetParameter(bw_h_in->name), &hidden, 0);
        data.Set(cell->GetParameter(bw_h_out->name), &hidden, 1);
      }
      for (int i = 0; i < FLAGS_repeat; ++i) {
        data.Compute();
      }

      Profile profile(&data);
      std::cout << profile.ASCIIReport() << "\n";
    }
  }

  return 0;
}

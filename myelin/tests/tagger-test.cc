#include <limits>
#include <string>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "base/init.h"
#include "base/flags.h"
#include "base/logging.h"
#include "file/file.h"
#include "myelin/compute.h"
#include "myelin/flow.h"
#include "myelin/graph.h"
#include "myelin/profile.h"
#include "myelin/kernel/avx.h"
#include "myelin/kernel/arithmetic.h"
#include "myelin/kernel/dragnn.h"
#include "myelin/kernel/generic.h"
#include "myelin/kernel/sse.h"
#include "third_party/jit/cpu.h"

DEFINE_int32(repeat, 1, "Number of times test is repeated");
DEFINE_bool(dump_flow, false, "Dump analyzed flow to stdout");
DEFINE_bool(dump_cell, false, "Dump network cell to stdout");
DEFINE_bool(dump_graph, true, "Dump dot graph");
DEFINE_bool(dump_code, true, "Dump generated code");

DEFINE_bool(sse, true, "SSE support");
DEFINE_bool(sse2, true, "SSE2 support");
DEFINE_bool(sse3, true, "SSE3 support");
DEFINE_bool(sse41, true, "SSE 4.1 support");
DEFINE_bool(avx, true, "AVX support");
DEFINE_bool(avx2, true, "AVX2 support");
DEFINE_bool(fma3, true, "FMA3 support");

using namespace sling;
using namespace sling::myelin;

struct RNNInstance;

// Stub for Dragnn initializer.
class FixedDragnnInitializer : public Kernel {
 public:
  string Name() override { return "WordInitializerDummy"; }
  string Operation() override { return "WordEmbeddingInitializer"; }

  bool Supports(Step *step) override { return true; }

  void Generate(Step *step, MacroAssembler *masm) override {}
};

// Type inference for Dragnn ops.
class FixedDragnnTyper : public Typer {
 public:
  bool InferTypes(Flow::Operation *op) override {
    if (op->type == "WordEmbeddingInitializer") {
      if (op->outdegree() == 1) {
        Flow::Variable *result = op->outputs[0];
        result->type = DT_INT32;
        result->shape.clear();
      }
    }

    return false;
  }
};

class RNN {
 public:
  // Profiling flag.
  static const bool profile = true;

  // Loads and initialize parser model.
  void Load(const string &filename);

  // Executes RNN over strings.
  void Execute(const std::vector<string> &tokens,
               std::vector<int> *predictions) const;

  // Looks up word in the lexicon.
  int LookupWord(const string &word) const;

  // Attaches connectors for LR LSTM.
  void AttachLR(RNNInstance *instance, int input, int output) const;

  // Extracts features for LR LSTM.
  void ExtractFeaturesLR(RNNInstance *instance, int current) const;

  // Output profile.
  void OutputProfile() const;

  // Loop up tag name.
  const string &tag(int index) const { return tags_[index]; }

 private:
  // Looks up cells, connectors, and parameters.
  Cell *GetCell(const string &name);
  Connector *GetConnector(const string &name);
  Tensor *GetParam(const string &name);

  // Tagger network.
  Library library_;
  Network network_;

  // Parser cells.
  Cell *lr_;  // left-to-right LSTM cell

  // Connectors.
  Connector *lr_c_;  // left-to-right LSTM control layer
  Connector *lr_h_;  // left-to-right LSTM hidden layer

  // Left-to-right LSTM network parameters and links.
  Tensor *lr_feature_words_;  // word feature
  Tensor *lr_c_in_;           // link to LSTM control input
  Tensor *lr_c_out_;          // link to LSTM control output
  Tensor *lr_h_in_;           // link to LSTM hidden input
  Tensor *lr_h_out_;          // link to LSTM hidden output
  Tensor *ff_output_;         // link to FF logit layer output

  // Lexicon.
  std::unordered_map<string, int> vocabulary_;
  int oov_ = -1;
  std::vector<string> tags_;
};

// RNN state for running an instance of the parser on a document.
struct RNNInstance {
 public:
  RNNInstance(Cell *lr, Connector *lr_c,
              Connector *lr_h, int begin, int end);

  // Instances for network computations.
  Instance lr;

  // Channels for connectors.
  Channel lr_c;
  Channel lr_h;

  // Word ids.
  std::vector<int> words;
};

void RNN::Load(const string &filename) {
  // Register kernels for implementing parser ops.
  RegisterAVXKernels(&library_);
  RegisterSSEKernels(&library_);
  RegisterDragnnKernels(&library_);
  library_.Register(new FixedDragnnInitializer());
  library_.RegisterTyper(new FixedDragnnTyper());
  RegisterArithmeticKernels(&library_);
  RegisterGenericKernels(&library_);
  RegisterGenericTransformations(&library_);

  // Load and analyze parser flow file.
  Flow flow;
  CHECK(flow.Load(filename));
  flow.Var("tagger/h_out")->out = true;
  flow.Var("tagger/c_out")->out = true;
  flow.Analyze(library_);

  // Output graph.
  if (FLAGS_dump_graph) {
    GraphOptions gopts;
    FlowToDotGraphFile(flow, gopts, "/tmp/tagger.dot");
  }

  if (FLAGS_dump_flow) {
    std::cout << flow.ToString() << "\n";
    std::cout.flush();
  }

  // Compile parser flow.
  if (profile) network_.set_profiling(true);
  CHECK(network_.Compile(flow, library_));

  // Get computation for each function.
  lr_ = GetCell("tagger");

  if (FLAGS_dump_code) {
    lr_->WriteCodeToFile("/tmp/tagger.bin");
  }
  if (FLAGS_dump_cell) {
    std::cout << lr_->ToString() << "\n";
    std::cout.flush();
  }

  // Get connectors.
  lr_c_ = GetConnector("tagger_c");
  lr_h_ = GetConnector("tagger_h");

  // Get LR LSTM parameters.
  lr_feature_words_ = GetParam("tagger/feature/words");
  lr_c_in_ = GetParam("tagger/c_in");
  lr_c_out_ = GetParam("tagger/c_out");
  lr_h_in_ = GetParam("tagger/h_in");
  lr_h_out_ = GetParam("tagger/h_out");

  ff_output_ = GetParam("tagger/output");

  // Load lexicon.
  Flow::Function *lexicon = flow.Func("lexicon");
  CHECK(lexicon != nullptr && lexicon->ops.size() == 1);
  const string &vocab = lexicon->ops[0]->GetAttr("dict");
  int pos = 0;
  int index = 0;
  string word;
  for (;;) {
    int next = vocab.find('\n', pos);
    if (next == -1) break;
    word.assign(vocab, pos, next - pos);
    if (word == "<UNKNOWN>") {
      oov_ = index++;
    } else {
      vocabulary_[word] = index++;
    }
    pos = next + 1;
  }
  string tagdata;
  CHECK(File::ReadContents("local/tag-map", &tagdata));
  pos = 0;
  string tag;
  for (;;) {
    int next = tagdata.find('\n', pos);
    if (next == -1) break;
    tag.assign(tagdata, pos, next - pos);
    tags_.push_back(tag);
    pos = next + 1;
  }
}

int RNN::LookupWord(const string &word) const {
  // Lookup word in vocabulary.
  auto f = vocabulary_.find(word);
  if (f != vocabulary_.end()) return f->second;

  // Check if word has digits.
  bool has_digits = false;
  for (char c : word) {
    if (c >= '0' && c <= '9') {
      has_digits = true;
    }
  }

  if (has_digits) {
    // Normalize digits and lookup the normalized word.
    string normalized = word;
    for (char &c : normalized) {
      if (c >= '0' && c <= '9') {
        c = '9';
      }
    }
    auto fn = vocabulary_.find(normalized);
    if (fn != vocabulary_.end()) return fn->second;
  }

  // Unknown word.
  return oov_;
}

void RNN::Execute(const std::vector<string> &tokens,
                  std::vector<int> *predictions) const {
  RNNInstance data(lr_, lr_c_, lr_h_, 0, tokens.size());
  for (int r = 0; r < FLAGS_repeat; ++r) {
    predictions->clear();

    // Look up words in vocabulary.
    for (int i = 0; i < tokens.size(); ++i) {
      int word = LookupWord(tokens[i]);
      data.words[i] = word;
    }

    // Compute left-to-right LSTM.
    for (int i = 0; i < tokens.size(); ++i) {
      // Attach hidden and control layers.
      //data.lr.Clear();
      int in = i > 0 ? i - 1 : tokens.size();
      int out = i;
      AttachLR(&data, in, out);

      // Extract features.
      ExtractFeaturesLR(&data, out);

      // Compute LSTM cell.
      data.lr.Compute();

      float *output = data.lr.Get<float>(ff_output_);
      int prediction = 0;
      float max_score = -std::numeric_limits<float>::infinity();

      for (int a = 0; a < ff_output_->dim(1); ++a) {
        if (output[a] > max_score) {
          prediction = a;
          max_score = output[a];
        }
      }
      predictions->push_back(prediction);
    }
  }

  if (profile) {
    Profile profile(&data.lr);
    std::cout << profile.ASCIIReport() << "\n";
  }
}

Cell *RNN::GetCell(const string &name) {
  Cell *cell = network_.GetCell(name);
  if (cell == nullptr) {
    LOG(FATAL) << "Unknown parser cell: " << name;
  }
  return cell;
}

Connector *RNN::GetConnector(const string &name) {
  Connector *cnx = network_.GetConnector(name);
  if (cnx == nullptr) {
    LOG(FATAL) << "Unknown parser connector: " << name;
  }
  return cnx;
}

Tensor *RNN::GetParam(const string &name) {
  Tensor *param = network_.GetParameter(name);
  if (param == nullptr) {
    LOG(FATAL) << "Unknown parser parameter: " << name;
  }
  return param;
}

RNNInstance::RNNInstance(Cell *lr,
                         Connector *lr_c,
                         Connector *lr_h, int begin, int end)
    : lr(lr), lr_c(lr_c), lr_h(lr_h) {
  // Allocate space for word ids.
  int length = end - begin;
  words.resize(length);

  // Add one extra element to LSTM activations for boundary element.
  this->lr_c.resize(length + 1);
  this->lr_h.resize(length + 1);
}

void RNN::AttachLR(RNNInstance *instance, int input, int output) const {
  instance->lr.Set(lr_c_in_, &instance->lr_c, input);
  instance->lr.Set(lr_c_out_, &instance->lr_c, output);
  instance->lr.Set(lr_h_in_, &instance->lr_h, input);
  instance->lr.Set(lr_h_out_, &instance->lr_h, output);
}

void RNN::ExtractFeaturesLR(RNNInstance *instance, int current) const {
  int word = instance->words[current];
  *instance->lr.Get<int>(lr_feature_words_) = word;
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  if (!FLAGS_sse) jit::CPU::Disable(jit::SSE);
  if (!FLAGS_sse2) jit::CPU::Disable(jit::SSE2);
  if (!FLAGS_sse3) jit::CPU::Disable(jit::SSE3);
  if (!FLAGS_sse41) jit::CPU::Disable(jit::SSE4_1);
  if (!FLAGS_avx) jit::CPU::Disable(jit::AVX);
  if (!FLAGS_avx2) jit::CPU::Disable(jit::AVX2);
  if (!FLAGS_fma3) jit::CPU::Disable(jit::FMA3);

  RNN rnn;
  const string testdata = "local/tagger_rnn.flow";
  rnn.Load(testdata);

  std::vector<string> sentence = {
    "John", "hit", "the", "ball", "with", "a", "bat",
  };
  std::vector<int> golden = {
    2, 10, 3, 0, 1, 3, 0,
  };

  std::vector<int> predictions;
  rnn.Execute(sentence, &predictions);

  for (int i = 0; i < predictions.size(); ++i) {
    LOG(INFO) << sentence[i] << " " << rnn.tag(predictions[i]);
  }

  CHECK_EQ(predictions.size(), sentence.size());
  for (int i = 0; i < predictions.size(); ++i) {
    CHECK_EQ(golden[i], predictions[i])
        << i << " gold: " << rnn.tag(golden[i])
        << " predicted: " << rnn.tag(predictions[i]);
  }

  return 0;
}


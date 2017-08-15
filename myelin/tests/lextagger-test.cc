#include <math.h>
#include <limits>
#include <string>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "base/clock.h"
#include "base/init.h"
#include "base/flags.h"
#include "base/logging.h"
#include "file/file.h"
#include "myelin/compute.h"
#include "myelin/dictionary.h"
#include "myelin/flow.h"
#include "myelin/graph.h"
#include "myelin/profile.h"
#include "myelin/kernel/tensorflow.h"
#include "myelin/kernel/dragnn.h"
#include "third_party/jit/cpu.h"

DEFINE_string(model, "local/tagger.flow", "Flow model for tagger");

DEFINE_int32(repeat, 1, "Number of times test is repeated");
DEFINE_bool(profile, false, "Profile computation");
DEFINE_bool(data_profile, false, "Output data instance profile");
DEFINE_bool(dynamic, false, "Dynamic instance allocation");
DEFINE_bool(dump_flow, false, "Dump analyzed flow to stdout");
DEFINE_bool(dump_cell, false, "Dump network cell to stdout");
DEFINE_bool(dump_graph, true, "Dump dot graph");
DEFINE_bool(dump_code, true, "Dump generated code");
DEFINE_bool(debug, false, "Debug mode");

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

class RNN {
 public:
  // Loads and initialize parser model.
  void Load(const string &filename);

  // Executes RNN over strings.
  void Execute(const std::vector<string> &tokens,
               std::vector<int> *predictions);

  // Looks up word in the lexicon.
  int LookupWord(const string &word) const;

  // Attaches connectors for LR LSTM.
  void AttachLR(RNNInstance *instance, int input, int output) const;

  // Extracts features for LR LSTM.
  void ExtractFeaturesLR(RNNInstance *instance, int current) const;

  // Loop up tag name.
  const string &tag(int index) const { return tags_[index]; }

  // Get tag id for tag name.
  int tagid(const string &tag) const {
    for (int i = 0; i < tags_.size(); ++i) {
      if (tag == tags_[i]) return i;
    }
    LOG(FATAL) << "Unknown tag name: " << tag;
    return -1;
  }

  void TestLexicon();
  std::vector<string> words;

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
  Dictionary lexicon_;
  std::unordered_map<string, int> vocabulary_;
  int oov_ = -1;
  std::vector<string> tags_;
};

// RNN state for running an instance of the parser on a document.
struct RNNInstance {
 public:
  RNNInstance(Cell *lr, Connector *lr_c,
              Connector *lr_h, int begin, int end);

  // Return tensor data.
  float *Get(const string &name) {
    Tensor *t = lr.cell()->GetParameter(name);
    CHECK(t != nullptr) << name;
    return t->ref() ? *lr.Get<float *>(t) : lr.Get<float>(t);
  }

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
  RegisterTensorflowLibrary(&library_);
  RegisterDragnnLibrary(&library_);

  // Load and patch flow file.
  LOG(INFO) << "Load";
  Flow flow;
  CHECK(flow.Load(filename));

  // Analyze flow.
  LOG(INFO) << "Analyze";
  flow.Analyze(library_);

  // Output flow.
  LOG(INFO) << "Dump";
  if (FLAGS_dump_flow) {
    std::cout << flow.ToString() << "\n";
    std::cout.flush();
  }

  // Output graph.
  if (FLAGS_dump_graph) {
    GraphOptions gopts;
    FlowToDotGraphFile(flow, gopts, "/tmp/tagger.dot");
  }

  // Compile parser flow.
  if (FLAGS_profile) network_.set_profiling(true);
  if (FLAGS_debug) network_.set_debug(true);
  if (FLAGS_dynamic) network_.set_dynamic_allocation(true);
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
  if (FLAGS_data_profile) {
    DataProfile dprof(lr_);
    File::WriteContents("/tmp/tagger-data.svg", dprof.AsSVG());
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
  Flow::Blob *dictionary = flow.DataBlock("dictionary");
  CHECK(dictionary != nullptr);
  lexicon_.Init(dictionary);

  const char *vocab = dictionary->data;
  const char *vend = vocab + dictionary->size;
  string word;
  int index = 0;
  while (vocab < vend) {
    const char *nl = vocab;
    while (nl < vend && *nl != '\n') nl++;
    if (nl == vend) break;
    word.assign(vocab, nl - vocab);
    if (word == "<UNKNOWN>") {
      oov_ = index++;
    } else {
      vocabulary_[word] = index++;
    }
    words.push_back(word);
    vocab = nl + 1;
  }
  if (oov_ == -1) oov_ = index - 1;

  // Load tag map.
  Flow::Blob *tagmap = flow.DataBlock("tags");
  CHECK(tagmap != nullptr);
  const char *tags = tagmap->data;
  const char *tend = tags + tagmap->size;
  string tag;
  while (tags < tend) {
    const char *nl = tags;
    while (nl < tend && *nl != '\n') nl++;
    if (nl == tend) break;
    tag.assign(tags, nl - tags);
    tags_.push_back(tag);
    tags = nl + 1;
  }
}

void RNN::TestLexicon() {
  const int repeat = 1000;

  LOG(INFO) << "Compare lookups";
  for (const string &word : words) {
    int slow = lexicon_.LookupSlow(word);
    int fast = lexicon_.Lookup(word);
    if (fast != slow) {
      LOG(ERROR) << "word " << word << " " << slow << " vs " << fast;
    }
  }

  Clock clock;
  double time;
  
  LOG(INFO) << "Benchmark hashmap";
  clock.start();
  for (int r = 0; r < repeat; ++r) {
    for (const string &word : words) {
      LookupWord(word);
    }
  }
  clock.stop();
  time = clock.ns() / (repeat * words.size());
  std::cout << "hashmap: " << time << " ns/lookup\n";

  LOG(INFO) << "Benchmark slow dictionary";
  clock.start();
  for (int r = 0; r < repeat; ++r) {
    for (const string &word : words) {
      lexicon_.LookupSlow(word);
    }
  }
  clock.stop();
  time = clock.ns() / (repeat * words.size());
  std::cout << "slow dictionary: " << time << " ns/lookup\n";

  LOG(INFO) << "Benchmark fast dictionary";
  clock.start();
  for (int r = 0; r < repeat; ++r) {
    for (const string &word : words) {
      lexicon_.Lookup(word);
    }
  }
  clock.stop();
  time = clock.ns() / (repeat * words.size());
  std::cout << "fast dictionary: " << time << " ns/lookup\n";
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
                  std::vector<int> *predictions) {
  RNNInstance data(lr_, lr_c_, lr_h_, 0, tokens.size());

  // Look up words in vocabulary.
  for (int i = 0; i < tokens.size(); ++i) {
    int word = LookupWord(tokens[i]);
    int lexword = lexicon_.Lookup(tokens[i]);
    data.words[i] = word;
    LOG(INFO) << tokens[i] << " " << word << " " << lexword;
  }

  Clock clock;
  clock.start();
  for (int r = 0; r < FLAGS_repeat; ++r) {
    // Compute left-to-right LSTM.
    predictions->clear();
    for (int i = 0; i < tokens.size(); ++i) {
      // Attach hidden and control layers.
      int in = i > 0 ? i - 1 : tokens.size() - 1;
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
  clock.stop();
  int64 n = FLAGS_repeat * tokens.size();
  LOG(INFO) << clock.cycles() / n << " cycles, " << clock.us() / n << " us";

  if (FLAGS_profile) {
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

  // Allocate channels for LSTM activations.
  this->lr_c.resize(length);
  this->lr_h.resize(length);
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

void ReadSentence(const string &sentence,
                  std::vector<string> *tokens,
                  std::vector<string> *tags) {
  int p = 0;
  for (;;) {
    int slash = sentence.find('/', p);
    CHECK(slash != -1);
    string token = sentence.substr(p, slash - p);
    p = sentence.find(' ', slash + 1);
    int space = (p == -1) ? sentence.size() : p;
    string tag = sentence.substr(slash + 1, space - slash - 1);
    tokens->push_back(token);
    tags->push_back(tag);
    if (p == -1) break;
    p = space + 1;
  }
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

  LOG(INFO) << "Compile tagger";
  RNN rnn;
  rnn.Load(FLAGS_model);

  //LOG(INFO) << "Test lexicon";
  //rnn.TestLexicon();

  string s;

  s = "John/NNP hit/VBD the/DT ball/NN with/IN a/DT bat/NN ./.";
  //s = "He/PRP was/VBD right/RB ./.";
  //s = "Such/JJ family/NN reunions/NN would/MD be/VB the/DT second/JJ "
  //    "since/IN 1945/CD ./.";
  //s = "Skipper/NNP 's/POS said/VBD the/DT merger/NN will/MD help/VB "
  //    "finance/VB remodeling/VBG and/CC future/JJ growth/NN ./.";
  //s = "I/RP much/RB prefer/VBP money/NN I/PRP can/MD put/VB my/PRP$ hands/NN "
  //    "on/IN ./. ''/''";

  std::vector<string> tokens;
  std::vector<string> tags;
  ReadSentence(s, &tokens, &tags);

  std::vector<int> golden;
  for (int i = 0; i < tokens.size(); ++i) {
    int t = rnn.tagid(tags[i]);
    CHECK(t != -1);
    golden.push_back(t);
  }

  LOG(INFO) << "Run tagger";
  std::vector<int> predictions;
  rnn.Execute(tokens, &predictions);
  LOG(INFO) << "Done";

  for (int i = 0; i < predictions.size(); ++i) {
    LOG(INFO) << tokens[i] << " " << rnn.tag(predictions[i]);
  }

  CHECK_EQ(tokens.size(), tokens.size());
  for (int i = 0; i < predictions.size(); ++i) {
    CHECK_EQ(golden[i], predictions[i])
        << i << " gold: " << rnn.tag(golden[i])
        << " predicted: " << rnn.tag(predictions[i]);
  }

  return 0;
}


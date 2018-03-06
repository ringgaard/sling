#include <stdio.h>
#include <string.h>
#include <iostream>
#include <random>
#include <string>

#include "sling/base/init.h"
#include "sling/base/flags.h"
#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/file/file.h"
#include "sling/myelin/builder.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/elf-linker.h"
#include "sling/myelin/flow.h"
#include "sling/myelin/gradient.h"
#include "sling/myelin/graph.h"
#include "sling/myelin/learning.h"
#include "sling/myelin/kernel/tensorflow.h"

DEFINE_bool(analyze, true, "Analyze flow");
DEFINE_bool(dump, false, "Dump flow");
DEFINE_bool(dump_cell, false, "Dump flow");

using namespace sling;
using namespace sling::myelin;

struct Token {
  Token(int word, int tag) : word(word), tag(tag) {}
  int word;
  int tag;
};

typedef std::vector<Token> Sentence;

struct Dictionary {
  int add(const string &word) {
    auto f = mapping.find(word);
    if (f != mapping.end()) return f->second;
    if (readonly) return -1;
    int index = vocabulary.size();
    mapping[word] = index;
    vocabulary.push_back(word);
    return index;
  }
  
  std::unordered_map<string, int> mapping;
  std::vector<string> vocabulary;
  bool readonly = false;
};

struct Corpus {
  ~Corpus() {
    for (auto *s : sentences) delete s;
  }

  Sentence *add() {
    Sentence *s = new Sentence();
    sentences.push_back(s);
    return s;
  }

  void Read(const  char *filename, Dictionary *words, Dictionary *tags) {
    char buffer[1024];
    FILE *f = fopen(filename, "r");
    Sentence *s = nullptr;
    while (fgets(buffer, 1024, f)) {
      char *nl = strchr(buffer, '\n');
      if (nl != nullptr) *nl = 0;
      if (*buffer == 0) {
        s = nullptr;
      } else {
        if (s == nullptr) s = add();
        char *tab = strchr(buffer, '\t');
        CHECK(tab != nullptr);
        int word = words->add(string(buffer, tab - buffer));
        int tag = tags->add(tab + 1);
        s->emplace_back(word, tag);
      }
    }
    fclose(f);
  }

  std::vector<Sentence *> sentences;
};

void RandomInitialize(Tensor *tensor, float stddev) {
  static std::mt19937 prng;
  std::normal_distribution<float> dist(0.0, stddev);
  TensorData data(tensor->data(), tensor);
  if (tensor->rank() == 1) {
    for (int r = 0; r < data.dim(0); ++r) {
      data.at<float>(r) = dist(prng);
    }
  } else if (tensor->rank() == 2) {
    for (int r = 0; r < data.dim(0); ++r) {
      for (int c = 0; c < data.dim(1); ++c) {
        data.at<float>(r, c) = dist(prng);
      }
    }
  } else {
    LOG(FATAL) << "Cannot initialize " << tensor->name();
  }
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Read training and test data.
  Dictionary words;
  words.add("<UNKNOWN>");
  Dictionary tags;

  Corpus train;
  train.Read("local/data/corpora/stanford/train.pos", &words, &tags);
  words.readonly = true;
  tags.readonly = true;

  Corpus dev;
  dev.Read("local/data/corpora/stanford/dev.pos", &words, &tags);

  LOG(INFO) << "Train sentences: " << train.sentences.size();  
  LOG(INFO) << "Dev sentences: " << dev.sentences.size();
  LOG(INFO) << "Words: " << words.vocabulary.size();
  LOG(INFO) << "Tags: " << tags.vocabulary.size();

  // Set up kernel library.
  Library library;
  RegisterTensorflowLibrary(&library);

  Network network;
  ElfLinker linker;
  network.set_linker(&linker);

  //jit::CPU::Enable(jit::AVX);
  //jit::CPU::Enable(jit::AVX2);
  //jit::CPU::Enable(jit::FMA3);

  // Build flow.
  int num_words = words.vocabulary.size();
  int num_tags = tags.vocabulary.size();
  int word_dim = 64;
  int lstm_dim = 128;
  Flow flow;
  Builder tf(&flow, "tagger");

  auto *word = tf.Placeholder("word", DT_INT32, {1, 1});
  auto *embedding = tf.Parameter("embedding", DT_FLOAT, {num_words, word_dim});
  auto *features = tf.Gather(embedding, word);

  auto *hidden = tf.LSTMLayer(features, lstm_dim);
  auto *logits = tf.FFLayer(hidden, num_tags, true);

  Flow::Function *dtagger = Gradient(&flow, tf.func(), library);

  CrossEntropyLoss loss;
  loss.Build(&flow, logits);

  GradientDescentOptimizer optimizer;
  optimizer.Build(&flow);

  LOG(INFO) << "logits: " << logits->name;
  LOG(INFO) << "dtagger: " << dtagger->name;

  // Analyze flow.
  if (FLAGS_analyze) {
    flow.Analyze(library);
  }

  // Dump flow.
  if (FLAGS_dump) {
    std::cout << flow.ToString();
  }

  // Output DOT graph.
  GraphOptions opts;
  FlowToDotGraphFile(flow, opts, "/tmp/postagger.dot");

  // Compile network.
  CHECK(network.Compile(flow, library));
  loss.Initialize(network);
  optimizer.Initialize(network);

  // Dump cell.
  for (Cell *cell : network.cells()) {
    if (FLAGS_dump_cell) {
      std::cout << cell->ToString();
    }
  }

  // Write object file with generated code.
  linker.Link();
  linker.Write("/tmp/postagger.o");

  // Initialize weights.
  for (Tensor *tensor : network.globals()) {
    if (tensor->learnable()) {
      LOG(INFO) << "Init " << tensor->name();
      RandomInitialize(tensor, 1e-4);
    }
  }

  // Create gradient instance for backpropagation.
  Instance gradients(network.GetCell(dtagger->name));
  
  // Create batch for loss accumulation.
  CrossEntropyLoss::Batch batch(loss);
  
  // Create channels.
  Connector *h_cnx = network.GetConnector("tagger/hidden");
  Connector *c_cnx = network.GetConnector("tagger/control");
  CHECK(h_cnx != nullptr);
  CHECK(c_cnx != nullptr);

  Channel h_zero(h_cnx);
  h_zero.resize(1);
  Channel c_zero(h_cnx);
  c_zero.resize(1);

  Channel h(h_cnx);
  Channel c(c_cnx);

  // Get cells and parameters.
  Cell *tagger = network.GetCell("tagger");
  Tensor *h_in = network.GetParameter("tagger/h_in");
  Tensor *h_out = network.GetParameter("tagger/h_out");
  Tensor *c_in = network.GetParameter("tagger/c_in");
  Tensor *c_out = network.GetParameter("tagger/c_out");
  Tensor *input = network.GetParameter("tagger/word");
  Tensor *output = network.GetParameter("tagger/logits");
  
  // Train.
  for (Sentence *sentence : train.sentences) {
    // Initialize training on sentence instance.
    int length = sentence->size();
    LOG(INFO) << "Train length " << length;
    h.resize(length + 1);
    c.resize(length + 1);
    batch.Clear();
    
    // Forward pass.
    std::vector<Instance *> forward;
    for (int i = 0; i < length; ++i) {
      Instance *data = new Instance(tagger);
      forward.push_back(data);
      if (i == 0) {
        data->Set(h_in, &h_zero);
        data->Set(c_in, &c_zero);
      } else {
        data->Set(h_in, &h, i);
        data->Set(c_in, &c, i);
      }
      data->Set(h_out, &h, i + 1);
      data->Set(c_out, &c, i + 1);
      *data->Get<int>(input) = (*sentence)[i].word;
      data->Compute();

      //LOG(INFO) << "data:\n" << data->ToString();
      //exit(1);

      // Compute loss.
      int target = (*sentence)[i].tag;
      batch.Forward(data->Get<float>(output), target);
    }

    batch.Backward();
    LOG(INFO) << "loss: " << batch.loss() << " batch size: " << batch.batch_size();
    // Clear data.
    for (auto *d : forward) delete d;
  }
  
  return 0;
}


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

// POS tagger flow using LSTM.
struct TaggerFlow : Flow {
  TaggerFlow(int num_words, int num_tags, int word_dim, int lstm_dim) {
		Builder tf(this, "tagger");
		tagger = tf.func();
		word = tf.Placeholder("word", DT_INT32, {1, 1});
		embedding = tf.Parameter("embedding", DT_FLOAT, {num_words, word_dim});
		features = tf.Gather(embedding, word);
		hidden = tf.LSTMLayer(features, lstm_dim);
		logits = tf.FFLayer(hidden, num_tags, true);
  }

  Function *tagger;
  Variable *word;
  Variable *embedding;
  Variable *features;
  Variable *hidden;
  Variable *logits;

  Function *dtagger;
  Variable *dlogits;
};

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

  // Build flow.
  int num_words = words.vocabulary.size();
  int num_tags = tags.vocabulary.size();
  int word_dim = 64;
  int lstm_dim = 128;
  TaggerFlow flow(num_words, num_tags, word_dim, lstm_dim);
  flow.dtagger = Gradient(&flow, flow.tagger, library);
  flow.dlogits = flow.Var("gradients/tagger/d_logits");
  flow.dlogits->ref = true;

  CrossEntropyLoss loss;
  loss.Build(&flow, flow.logits, flow.dlogits);

  GradientDescentOptimizer optimizer;
  optimizer.Build(&flow);

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

  // Create batch for loss accumulation.
  CrossEntropyLoss::Batch batch(loss);

  // Create channels.
  Connector *h_cnx = network.GetConnector("tagger/cnx_hidden");
  Connector *c_cnx = network.GetConnector("tagger/cnx_control");
  CHECK(h_cnx != nullptr);
  CHECK(c_cnx != nullptr);

  Channel h_zero(h_cnx);
  h_zero.resize(1);
  Channel c_zero(h_cnx);
  c_zero.resize(1);

  Channel h(h_cnx);
  Channel c(c_cnx);

  Channel dh(h_cnx);
  Channel dc(c_cnx);

  // Get cells and parameters.
  Cell *tagger = network.GetCell("tagger");
  Tensor *h_in = network.GetParameter("tagger/h_in");
  Tensor *h_out = network.GetParameter("tagger/h_out");
  Tensor *c_in = network.GetParameter("tagger/c_in");
  Tensor *c_out = network.GetParameter("tagger/c_out");
  Tensor *input = network.GetParameter("tagger/word");
  Tensor *output = network.GetParameter("tagger/logits");

  Cell *dtagger = network.GetCell("gradients/tagger");
  Tensor *primal = network.GetParameter("gradients/tagger/primal");
  Tensor *dh_in = network.GetParameter("gradients/tagger/d_h_in");
  Tensor *dh_out = network.GetParameter("gradients/tagger/d_h_out");
  Tensor *dc_in = network.GetParameter("gradients/tagger/d_c_in");
  Tensor *dc_out = network.GetParameter("gradients/tagger/d_c_out");
  Tensor *dlogits = network.GetParameter("gradients/tagger/d_logits");

  // Allocate gradients.
  Instance gtagger(dtagger);
  std::vector<Instance *> gradients = {&gtagger};

  // Train.
  int num_tokens = 0;
  int num_sentences = 0;
  LOG(INFO) << "Start training";
  for (Sentence *sentence : train.sentences) {
    // Initialize training on sentence instance.
    int length = sentence->size();
    h.resize(length + 1);
    c.resize(length + 1);
    batch.Clear();
    gtagger.Clear();

    // Forward pass.
    std::vector<Instance *> forward;
    for (int i = 0; i < length; ++i) {
      // Create new instance for token.
      Instance *data = new Instance(tagger);
      forward.push_back(data);

      // Set up channels.
      if (i == 0) {
        data->Set(h_in, &h_zero);
        data->Set(c_in, &c_zero);
      } else {
        data->Set(h_in, &h, i);
        data->Set(c_in, &c, i);
      }
      data->Set(h_out, &h, i + 1);
      data->Set(c_out, &c, i + 1);

      // Set up features.
      *data->Get<int>(input) = (*sentence)[i].word;

      // Compute forward.
      data->Compute();

      //LOG(INFO) << "data:\n" << data->ToString();
      //exit(1);

      // Accumulate loss.
      int target = (*sentence)[i].tag;
      batch.Forward(data->Get<float>(output), target);
    }

    // Compute batch loss and loss gradient.
    batch.Backward();
    LOG(INFO) << "loss: " << batch.loss() << " batch size: " << batch.batch_size();

    // Backpropagate gradients.
    dh.resize(length + 1);
    dc.resize(length + 1);
    for (int i = length - 1; i >= 0; --i) {
      // Set gradient.
      gtagger.SetReference(dlogits, batch.dlogits());

      // Set reference to primal cell.
      gtagger.Set(primal, forward[i]);

      // Set up channels.
      if (i == 0) {
        gtagger.Set(dh_out, &h_zero);
        gtagger.Set(dc_out, &c_zero);
      } else {
        gtagger.Set(dh_out, &dh, i + 1);
        gtagger.Set(dc_out, &dc, i + 1);
      }
      gtagger.Set(dh_in, &dh, i);
      gtagger.Set(dc_in, &dc, i);

      // Compute backward.
      gtagger.Compute();
    }

    // Apply gradients.
    optimizer.Apply(gradients);

    // Clear data.
    for (auto *d : forward) delete d;

    num_sentences++;
    num_tokens += length;
    if (num_sentences % 1000 == 0) {
      LOG(INFO) << num_sentences << " sentences, " << num_tokens << " tokens";
    }
  }

  return 0;
}


#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
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
#include "sling/myelin/profile.h"
#include "sling/myelin/kernel/tensorflow.h"

DEFINE_bool(dump, false, "Dump flow");
DEFINE_bool(dump_cell, false, "Dump flow");
DEFINE_bool(profile, false, "Profile tagger");
DEFINE_int32(epochs, 250000, "Number of training epochs");
DEFINE_int32(report, 1000, "Report status after every n sentence");
DEFINE_double(alpha, 1.0, "Learning rate");
DEFINE_double(decay, 0.5, "Learning rate decay rate");
DEFINE_double(clip, 1.0, "Gradient norm clipping");
DEFINE_int32(seed, 0, "Random number generator seed");
DEFINE_int32(alpha_update, 50000, "Number of epochs between alpha updates");
DEFINE_int32(batch, 128, "Number of epochs between gradient updates");
DEFINE_bool(shuffle, true, "Shuffle training corpus");
DEFINE_bool(heldout, true, "Test tagger on heldout data");

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

void DumpProfile(ProfileSummary *summary) {
  Profile profile(summary);
  string report = profile.ASCIIReport();
  std::cout << report << "\n";
}

// POS tagger flow.
struct TaggerFlow : Flow {
  TaggerFlow(Library &library,
             int num_words,
             int num_tags,
             int word_dim,
             int lstm_dim) {
    // Build forward flow.
    Builder tf(this, "tagger");
    tagger = tf.func();
    word = tf.Placeholder("word", DT_INT32, {1, 1});
    embedding = tf.Parameter("embedding", DT_FLOAT, {num_words, word_dim});
    features = tf.Gather(embedding, word);
    hidden = tf.LSTMLayer(features, lstm_dim);
    logits = tf.FFLayer(hidden, num_tags, true);

    // Build gradient for tagger.
    dtagger = Gradient(this, tagger, library);
    dlogits = Var("gradients/tagger/d_logits");
    dlogits->ref = true;
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

// POS tagger model.
struct TaggerModel {
  TaggerModel(Network *network) {
    tagger = network->GetCell("tagger");
    h_in = network->GetParameter("tagger/h_in");
    h_out = network->GetParameter("tagger/h_out");
    c_in = network->GetParameter("tagger/c_in");
    c_out = network->GetParameter("tagger/c_out");
    word = network->GetParameter("tagger/word");
    logits = network->GetParameter("tagger/logits");

    dtagger = network->GetCell("gradients/tagger");
    primal = network->GetParameter("gradients/tagger/primal");
    dh_in = network->GetParameter("gradients/tagger/d_h_in");
    dh_out = network->GetParameter("gradients/tagger/d_h_out");
    dc_in = network->GetParameter("gradients/tagger/d_c_in");
    dc_out = network->GetParameter("gradients/tagger/d_c_out");
    dlogits = network->GetParameter("gradients/tagger/d_logits");

    h_cnx = network->GetConnector("tagger/cnx_hidden");
    c_cnx = network->GetConnector("tagger/cnx_control");
    l_cnx = network->GetConnector("loss/cnx_dlogits");
  }

  // Forward parameters.
  Cell *tagger;
  Tensor *h_in;
  Tensor *h_out;
  Tensor *c_in;
  Tensor *c_out;
  Tensor *word;
  Tensor *logits;

  // Backward parameters.
  Cell *dtagger;
  Tensor *primal;
  Tensor *dh_in;
  Tensor *dh_out;
  Tensor *dc_in;
  Tensor *dc_out;
  Tensor *dlogits;

  // Connectors.
  Connector *h_cnx;
  Connector *c_cnx;
  Connector *l_cnx;
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
  if (FLAGS_profile) {
    network.options().profiling = true;
    network.options().external_profiler = true;
  }

  // Build flow for POS tagger.
  int num_words = words.vocabulary.size();
  int num_tags = tags.vocabulary.size();
  int word_dim = 64;
  int lstm_dim = 128;
  TaggerFlow flow(library, num_words, num_tags, word_dim, lstm_dim);

  CrossEntropyLoss loss;
  loss.Build(&flow, flow.logits, flow.dlogits);

  GradientDescentOptimizer optimizer;
  optimizer.set_clipping_threshold(FLAGS_clip);
  optimizer.Build(&flow);

  // Analyze flow.
  flow.Analyze(library);

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

  // Dump cells.
  if (FLAGS_dump_cell) {
    for (Cell *cell : network.cells()) std::cout << cell->ToString();
  }

  // Write object file with generated code.
  linker.Link();
  linker.Write("/tmp/postagger.o");

  // Set up model.
  TaggerModel model(&network);

  // Initialize weights.
  std::mt19937 prng(FLAGS_seed);
  std::normal_distribution<float> normal(0.0, 1e-4);
  for (Tensor *tensor : network.globals()) {
    if (tensor->learnable() && tensor->rank() == 2) {
      TensorData data(tensor->data(), tensor);
      for (int r = 0; r < data.dim(0); ++r) {
        for (int c = 0; c < data.dim(1); ++c) {
          data.at<float>(r, c) = normal(prng);
        }
      }
    }
  }

  // Create channels.
  Channel h_zero(model.h_cnx);
  Channel c_zero(model.c_cnx);
  h_zero.resize(1);
  c_zero.resize(1);

  Channel h(model.h_cnx);
  Channel c(model.c_cnx);
  Channel l(model.l_cnx);
  Channel dh(model.h_cnx);
  Channel dc(model.c_cnx);

  // Profiling.
  ProfileSummary tagger_profile(model.tagger);
  ProfileSummary dtagger_profile(model.dtagger);

  // Allocate gradients.
  Instance gtagger(model.dtagger);
  std::vector<Instance *> gradients = {&gtagger};

  // Train.
  LOG(INFO) << "Start training";
  int prev_tokens = 0;
  int num_tokens = 0;
  float loss_sum = 0.0;
  int loss_count = 0;
  float alpha = FLAGS_alpha;
  clock_t start = clock();
  for (int epoch = 1; epoch <= FLAGS_epochs; ++epoch) {
    // Select next sentence to train on.
    int sample = (FLAGS_shuffle ? prng() : epoch - 1) % train.sentences.size();
    Sentence *sentence = train.sentences[sample];

    // Initialize training on sentence instance.
    int length = sentence->size();
    h.resize(length + 1);
    c.resize(length + 1);
    l.resize(length);

    // Forward pass.
    std::vector<Instance *> forward;
    for (int i = 0; i < length; ++i) {
      // Create new instance for token.
      Instance *data = new Instance(model.tagger);
      forward.push_back(data);
      if (FLAGS_profile) data->set_profile(&tagger_profile);

      // Set up channels.
      if (i == 0) {
        data->Set(model.h_in, &h_zero);
        data->Set(model.c_in, &c_zero);
      } else {
        data->Set(model.h_in, &h, i);
        data->Set(model.c_in, &c, i);
      }
      data->Set(model.h_out, &h, i + 1);
      data->Set(model.c_out, &c, i + 1);

      // Set up features.
      *data->Get<int>(model.word) = (*sentence)[i].word;

      // Compute forward.
      data->Compute();

      // Compute loss and gradient.
      int target = (*sentence)[i].tag;
      float *logits = data->Get<float>(model.logits);
      float *dlogits = reinterpret_cast<float *>(l.at(i));
      float cost = loss.Compute(logits, target, dlogits);
      loss_sum += cost;
      loss_count++;
    }

    // Backpropagate loss gradient.
    dh.resize(length);
    dc.resize(length);
    if (FLAGS_profile) gtagger.set_profile(&dtagger_profile);
    for (int i = length - 1; i >= 0; --i) {
      // Set gradient.
      gtagger.Set(model.dlogits, &l, i);

      // Set reference to primal cell.
      gtagger.Set(model.primal, forward[i]);

      // Set up channels.
      if (i == length - 1) {
        gtagger.Set(model.dh_out, &h_zero);
        gtagger.Set(model.dc_out, &c_zero);
      } else {
        gtagger.Set(model.dh_out, &dh, i + 1);
        gtagger.Set(model.dc_out, &dc, i + 1);
      }
      gtagger.Set(model.dh_in, &dh, i);
      gtagger.Set(model.dc_in, &dc, i);

      // Compute backward.
      gtagger.Compute();
    }

    // Clear data.
    for (auto *d : forward) delete d;
    forward.clear();
    num_tokens += length;

    // Decay learning rate.
    if (epoch % FLAGS_alpha_update == 0) {
      alpha *= FLAGS_decay;
    }

    // Apply gradients to model.
    if (epoch % FLAGS_batch == 0) {
      optimizer.set_alpha(alpha);
      optimizer.Apply(gradients);
      gtagger.Clear();
    }

    // Report progress.
    if (epoch % FLAGS_report == 0) {
      clock_t end = clock();
      float avg_loss = loss_sum / loss_count;
      float acc = exp(-avg_loss) * 100.0;
      if (FLAGS_heldout) {
        // Compute accuracy on dev corpus.
        int num_correct = 0;
        int num_wrong = 0;
        Instance test(model.tagger);
        if (FLAGS_profile) test.set_profile(&tagger_profile);
        for (Sentence *s : dev.sentences) {
          int length = s->size();
          h.resize(length + 1);
          c.resize(length + 1);
          for (int i = 0; i < length; ++i) {
            // Set up channels.
            if (i == 0) {
              test.Set(model.h_in, &h_zero);
              test.Set(model.c_in, &c_zero);
            } else {
              test.Set(model.h_in, &h, i);
              test.Set(model.c_in, &c, i);
            }
            test.Set(model.h_out, &h, i + 1);
            test.Set(model.c_out, &c, i + 1);

            // Set up features.
            int word = (*s)[i].word;
            *test.Get<int>(model.word) = word;

            // Compute forward.
            test.Compute();

            // Compute predicted tag.
            float *predictions = test.Get<float>(model.logits);
            int best = 0;
            for (int t = 1; t < num_tags; ++t) {
              if (predictions[t] > predictions[best]) best = t;
            }

            // Compare with golden tag.
            int target = (*s)[i].tag;
            if (best == target) {
              num_correct++;
            } else {
              num_wrong++;
            }
          }
        }

        acc = num_correct * 100.0 / (num_correct + num_wrong);
      }

      float secs = (end - start) / 1000000.0;
      int tps = (num_tokens - prev_tokens) / secs;
      LOG(INFO) << "epochs " << epoch << ", "
                << "alpha " << alpha << ", "
                << tps << " tokens/s, loss=" << avg_loss
                << ", accuracy=" << acc;
      loss_sum = 0.0;
      loss_count = 0;
      start = clock();
      prev_tokens = num_tokens;
    }
  }

  if (FLAGS_profile) {
    DumpProfile(&tagger_profile);
    DumpProfile(&dtagger_profile);
    DumpProfile(loss.profile());
    DumpProfile(optimizer.profile());
  }

  return 0;
}


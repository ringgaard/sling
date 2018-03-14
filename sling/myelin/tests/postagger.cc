#include <math.h>
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
#include "sling/myelin/profile.h"
#include "sling/myelin/kernel/tensorflow.h"

DEFINE_bool(dump, false, "Dump flow");
DEFINE_bool(dump_cell, false, "Dump flow");
DEFINE_bool(profile, false, "Profile tagger");
DEFINE_int32(epochs, 500000, "Number of training epochs");
DEFINE_int32(report, 1000, "Report status after every n sentence");
DEFINE_double(alpha, 0.1, "Learning rate");
DEFINE_double(decay, 0.5, "Learning rate decay rate");
DEFINE_int32(seed, 0, "Random number generator seed");
DEFINE_int32(alpha_update, 50000, "Number of epochs between alpha updates");
DEFINE_int32(batch, 1, "Number of epochs between gradient updates");
DEFINE_bool(shuffle, true, "Shuffle training corpus");

#define BATCHSPLIT

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
  static std::mt19937 prng(FLAGS_seed);
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

float MaxAbs(const TensorData &data) {
  float max = 0.0;
  if (data.rank() == 1) {
    for (int r = 0; r < data.dim(0); ++r) {
      float f = fabs(data.at<float>(r));
      if (f > max) max = f;
    }
  } else if (data.rank() == 2) {
    for (int r = 0; r < data.dim(0); ++r) {
      for (int c = 0; c < data.dim(1); ++c) {
        float f = fabs(data.at<float>(r, c));
        if (f > max) max = f;
      }
    }
  } else {
    return -1;
  }
  return max;
}

void DumpProfile(ProfileSummary *summary) {
  Profile profile(summary);
  string report = profile.ASCIIReport();
  std::cout << report << "\n";
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

  //jit::CPU::Disable(jit::AVX2);
  //jit::CPU::Disable(jit::FMA3);

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
  CHECK(flow.IsConsistent());
  flow.Analyze(library);
  CHECK(flow.IsConsistent());

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
      RandomInitialize(tensor, 1e-4);
    }
  }

  // Create batch for loss accumulation.
  CrossEntropyLoss::Batch batch(loss);

  // Create channels.
  Connector *h_cnx = network.GetConnector("tagger/cnx_hidden");
  Connector *c_cnx = network.GetConnector("tagger/cnx_control");
  Connector *l_cnx = network.GetConnector("loss/cnx_dlogits");

  Channel h_zero(h_cnx);
  h_zero.resize(1);
  Channel c_zero(c_cnx);
  c_zero.resize(1);

  Channel h(h_cnx);
  Channel c(c_cnx);
  Channel l(l_cnx);
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

  //Tensor *dx2i = network.GetParameter("gradients/tagger/d_x2i");

  // Profiling.
  ProfileSummary tagger_profile(tagger);
  ProfileSummary dtagger_profile(dtagger);

  // Allocate gradients.
#ifdef BATCHSPLIT
  std::vector<Instance *> gtaggers;
  for (int i = 0; i < FLAGS_batch; ++i) {
    gtaggers.push_back(new Instance(dtagger));
  }
#else
  Instance gtagger(dtagger);
  std::vector<Instance *> gradients = {&gtagger};
#endif

  // Train.
  int num_tokens = 0;
  float loss_sum = 0.0;
  int loss_count = 0;
  float alpha = FLAGS_alpha;
  std::mt19937 prng(FLAGS_seed);

  LOG(INFO) << "Start training";
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
      Instance *data = new Instance(tagger);
      forward.push_back(data);
      if (FLAGS_profile) data->set_profile(&tagger_profile);

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

      // Compute loss and gradient.
      int target = (*sentence)[i].tag;
      batch.Clear();
      batch.Forward(data->Get<float>(output), target);
      float *dl = reinterpret_cast<float *>(l.at(i));
      batch.Backward(dl);
      loss_sum += batch.loss();
      loss_count++;
    }

    // Backpropagate loss gradient.
    dh.resize(length);
    dc.resize(length);
#ifdef BATCHSPLIT
    Instance &gtagger = *gtaggers[epoch % FLAGS_batch];
#endif
    if (FLAGS_profile) gtagger.set_profile(&dtagger_profile);
    for (int i = length - 1; i >= 0; --i) {
      // Set gradient.
      gtagger.Set(dlogits, &l, i);

      // Set reference to primal cell.
      gtagger.Set(primal, forward[i]);

      // Set up channels.
      if (i == length - 1) {
        LOG(INFO) << "zero channels";
        gtagger.Set(dh_out, &h_zero);
        gtagger.Set(dc_out, &c_zero);
        LOG(INFO) << "hzero=" << gtagger.ToString(dh_out);
        LOG(INFO) << "czero=" << gtagger.ToString(dc_out);
      } else {
        LOG(INFO) << "channels " <<  (i + 1) << " of " << dh.size();
        gtagger.Set(dh_out, &dh, i + 1);
        gtagger.Set(dc_out, &dc, i + 1);
      }
      gtagger.Set(dh_in, &dh, i);
      gtagger.Set(dc_in, &dc, i);

      // Compute backward.
      gtagger.Compute();

      LOG(INFO) << "Max gradients at epoch " << epoch;
      for (Tensor *tensor : network.parameters()) {
        if (tensor->cell() == dtagger && tensor->name().find("/d_") != -1) {
          LOG(INFO) << "max gradient " << tensor->name() << " = " << MaxAbs(gtagger[tensor]);
        }
      }

#if 0
      if (num_tokens == 6037) {
        batch.dump();

        LOG(INFO) << "forward " << num_tokens << " " << i << "\n" << forward[i]->ToString();

        LOG(INFO) << "back " << num_tokens << " " << i;
        for (Tensor *t : network.parameters()) {
          if (t->cell() == dtagger && t->shared() == nullptr) {
            if (t->name() == "gradients/tagger/d_embedding") continue;
            LOG(INFO) << t->name() << " = " << gtagger.ToString(t).substr(0, 100);
          }
        }

        LOG(INFO) << "dlogits=" << gtagger["gradients/tagger/d_logits"].ToString();
      }

      float *f = gtagger.Get<float>(dx2i);
      CHECK(!std::isnan(*f)) << num_tokens << " " << i;
#endif
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
#if 0
      LOG(INFO) << "gtagger " << num_tokens;
      for (Tensor *t : network.parameters()) {
        if (t->cell() == dtagger && t->ref()) {
          gtagger.SetReference(t, nullptr);
        }
      }
      for (Tensor *t : network.parameters()) {
        if (t->cell() == dtagger && t->shared() == nullptr) {
          if (t->name() == "gradients/tagger/d_embedding") continue;
          LOG(INFO) << t->name() << " = " << gtagger.ToString(t).substr(0, 100);
        }
      }

      //TensorData W0 = network["tagger/W0"];
      //LOG(INFO) << "W0=" << W0.ToString();
#endif

      optimizer.set_alpha(alpha);
#ifdef BATCHSPLIT
      for (int i = 0; i < FLAGS_batch; ++i) {
        std::vector<Instance *> gradients = {gtaggers[i]};

        optimizer.Apply(gradients);
        gtaggers[i]->Clear();
      }
#else
      optimizer.Apply(gradients);
      gtagger.Clear();
#endif
    }

    // Report progress.
    if (epoch % FLAGS_report == 0) {
      // Compute accuracy on dev corpus.
      int num_correct = 0;
      int num_wrong = 0;
      Instance test(tagger);
      if (FLAGS_profile) test.set_profile(&tagger_profile);
      for (Sentence *s : dev.sentences) {
        int length = s->size();
        h.resize(length + 1);
        c.resize(length + 1);
        for (int i = 0; i < length; ++i) {
          // Set up channels.
          if (i == 0) {
            test.Set(h_in, &h_zero);
            test.Set(c_in, &c_zero);
          } else {
            test.Set(h_in, &h, i);
            test.Set(c_in, &c, i);
          }
          test.Set(h_out, &h, i + 1);
          test.Set(c_out, &c, i + 1);

          // Set up features.
          int word = (*s)[i].word;
          *test.Get<int>(input) = word;

          // Compute forward.
          test.Compute();

          // Compute predicted tag.
          float *predictions = test.Get<float>(output);
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

      float acc = num_correct * 100.0 / (num_correct + num_wrong);
      float avg_loss = loss_sum / loss_count;
      LOG(INFO) << "epochs " << epoch << ", "
                << "alpha " << alpha << ", "
                << num_tokens << " tokens, loss=" << avg_loss
                << ", accuracy=" << acc;
      loss_sum = 0.0;
      loss_count = 0;
    }
  }

  if (FLAGS_profile) {
    DumpProfile(&tagger_profile);
    DumpProfile(&dtagger_profile);
    DumpProfile(batch.forward_profile());
    DumpProfile(batch.backward_profile());
    DumpProfile(optimizer.profile());
  }

  return 0;
}


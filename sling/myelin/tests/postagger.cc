#include <math.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <random>
#include <string>
#include <sys/time.h>
#include <unistd.h>

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
#include "sling/util/mutex.h"
#include "sling/util/thread.h"

DEFINE_bool(dump, false, "Dump flow");
DEFINE_bool(dump_cell, false, "Dump flow");
DEFINE_bool(profile, false, "Profile tagger");
DEFINE_int32(epochs, 250000, "Number of training epochs");
DEFINE_int32(report, 1000, "Report status after every n sentence");
DEFINE_double(alpha, 1.0, "Learning rate");
DEFINE_double(lambda, 0.0, "Regularization parameter");
DEFINE_double(decay, 0.5, "Learning rate decay rate");
DEFINE_double(clip, 1.0, "Gradient norm clipping");
DEFINE_int32(seed, 0, "Random number generator seed");
DEFINE_int32(alpha_update, 50000, "Number of epochs between alpha updates");
DEFINE_int32(batch, 128, "Number of epochs between gradient updates");
DEFINE_bool(shuffle, true, "Shuffle training corpus");
DEFINE_bool(heldout, true, "Test tagger on heldout data");
DEFINE_int32(threads, 1, "Number of threads for training");
DEFINE_int32(rampup, 0, "Number of seconds between thread starts");
DEFINE_bool(lock, false, "Locked gradient updates");

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

double WallTime() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// POS tagger flow.
struct TaggerFlow : Flow {
  void Build(Library &library,
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
  ~TaggerModel() {
    delete tagger_profile;
    delete dtagger_profile;
  }

  void Initialize(Network &network) {
    tagger = network.GetCell("tagger");
    h_in = network.GetParameter("tagger/h_in");
    h_out = network.GetParameter("tagger/h_out");
    c_in = network.GetParameter("tagger/c_in");
    c_out = network.GetParameter("tagger/c_out");
    word = network.GetParameter("tagger/word");
    logits = network.GetParameter("tagger/logits");

    dtagger = network.GetCell("gradients/tagger");
    primal = network.GetParameter("gradients/tagger/primal");
    dh_in = network.GetParameter("gradients/tagger/d_h_in");
    dh_out = network.GetParameter("gradients/tagger/d_h_out");
    dc_in = network.GetParameter("gradients/tagger/d_c_in");
    dc_out = network.GetParameter("gradients/tagger/d_c_out");
    dlogits = network.GetParameter("gradients/tagger/d_logits");

    h_cnx = network.GetConnector("tagger/cnx_hidden");
    c_cnx = network.GetConnector("tagger/cnx_control");
    l_cnx = network.GetConnector("loss/cnx_dlogits");

    if (tagger->profile()) tagger_profile = new ProfileSummary(tagger);
    if (dtagger->profile()) dtagger_profile = new ProfileSummary(dtagger);
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

  // Profiling.
  ProfileSummary *tagger_profile = nullptr;
  ProfileSummary *dtagger_profile = nullptr;
};

// POS tagger.
class Tagger {
 public:
  Tagger() {
    // Set up kernel library.
    RegisterTensorflowLibrary(&library_);

    network_.set_linker(&linker_);
    if (FLAGS_profile) {
      network_.options().profiling = true;
      network_.options().external_profiler = true;
    }
  }

  // Read training and test corpora.
  void ReadCorpora() {
    words_.add("<UNKNOWN>");
    train_.Read("local/data/corpora/stanford/train.pos", &words_, &tags_);
    words_.readonly = true;
    tags_.readonly = true;
    dev_.Read("local/data/corpora/stanford/dev.pos", &words_, &tags_);

    num_words_ = words_.vocabulary.size();
    num_tags_ = tags_.vocabulary.size();

    LOG(INFO) << "Train sentences: " << train_.sentences.size();
    LOG(INFO) << "Dev sentences: " << dev_.sentences.size();
    LOG(INFO) << "Words: " << words_.vocabulary.size();
    LOG(INFO) << "Tags: " << tags_.vocabulary.size();
  }

  // Build tagger flow.
  void Build() {
    // Build flow for POS tagger.
    flow_.Build(library_, num_words_, num_tags_, word_dim_, lstm_dim_);

    // Build loss computation.
    loss_.Build(&flow_, flow_.logits, flow_.dlogits);

    // Build optimizer.
    optimizer_.set_clipping_threshold(FLAGS_clip);
    optimizer_.set_lambda(FLAGS_lambda);
    optimizer_.Build(&flow_);
  }

  // Compile model.
  void Compile() {
    // Analyze flow.
    flow_.Analyze(library_);

    // Dump flow.
    if (FLAGS_dump) {
      std::cout << flow_.ToString();
    }

    // Output DOT graph.
    GraphOptions opts;
    FlowToDotGraphFile(flow_, opts, "/tmp/postagger.dot");

    // Compile network.
    CHECK(network_.Compile(flow_, library_));

    // Dump cells.
    if (FLAGS_dump_cell) {
      for (Cell *cell : network_.cells()) std::cout << cell->ToString();
    }

    // Write object file with generated code.
    linker_.Link();
    linker_.Write("/tmp/postagger.o");

    // Initialize model.
    model_.Initialize(network_);
    loss_.Initialize(network_);
    optimizer_.Initialize(network_);
  }

  // Initialize model weights.
  void Initialize() {
    std::mt19937 prng(FLAGS_seed);
    std::normal_distribution<float> normal(0.0, 1e-4);
    for (Tensor *tensor : network_.globals()) {
      if (tensor->learnable() && tensor->rank() == 2) {
        TensorData data(tensor->data(), tensor);
        for (int r = 0; r < data.dim(0); ++r) {
          for (int c = 0; c < data.dim(1); ++c) {
            data.at<float>(r, c) = normal(prng);
          }
        }
      }
    }
  }

  // Train model.
  void Train() {
    // Start training workers.
    LOG(INFO) << "Start training";
    start_ = WallTime();
    WorkerPool pool;
    pool.Start(FLAGS_threads, [this](int index) { Worker(index);});

    // Wait until workers completes.
    pool.Join();
  }

  // Trainer worker thread.
  void Worker(int index) {
    // Ramp-up peiod.
    sleep(index * FLAGS_rampup);
    LOG(INFO) << "Start worker " << index;

    // Create channels.
    Channel h_zero(model_.h_cnx);
    Channel c_zero(model_.c_cnx);
    h_zero.resize(1);
    c_zero.resize(1);
    Channel h(model_.h_cnx);
    Channel c(model_.c_cnx);
    Channel l(model_.l_cnx);
    Channel dh(model_.h_cnx);
    Channel dc(model_.c_cnx);

    // Allocate gradients.
    Instance gtagger(model_.dtagger);
    std::vector<Instance *> gradients = {&gtagger};

    std::mt19937 prng(FLAGS_seed + index);
    int num_sentences = train_.sentences.size();
    int iteration = 0;
    float local_loss_sum = 0.0;
    int local_loss_count = 0;
    while (true) {
      // Select next sentence to train on.
      int sample = (FLAGS_shuffle ? prng() : iteration) % num_sentences;
      Sentence *sentence = train_.sentences[sample];
      iteration++;

      // Initialize training on sentence instance.
      int length = sentence->size();
      h.resize(length + 1);
      c.resize(length + 1);
      l.resize(length);

      // Forward pass.
      std::vector<Instance *> forward;
      for (int i = 0; i < length; ++i) {
        // Create new instance for token.
        Instance *data = new Instance(model_.tagger);
        forward.push_back(data);
        if (FLAGS_profile) data->set_profile(model_.tagger_profile);

        // Set up channels.
        if (i == 0) {
          data->Set(model_.h_in, &h_zero);
          data->Set(model_.c_in, &c_zero);
        } else {
          data->Set(model_.h_in, &h, i);
          data->Set(model_.c_in, &c, i);
        }
        data->Set(model_.h_out, &h, i + 1);
        data->Set(model_.c_out, &c, i + 1);

        // Set up features.
        *data->Get<int>(model_.word) = (*sentence)[i].word;

        // Compute forward.
        data->Compute();

        // Compute loss and gradient.
        int target = (*sentence)[i].tag;
        float *logits = data->Get<float>(model_.logits);
        float *dlogits = reinterpret_cast<float *>(l.at(i));
        float cost = loss_.Compute(logits, target, dlogits);
        local_loss_sum += cost;
        local_loss_count++;
      }

      // Backpropagate loss gradient.
      dh.resize(length);
      dc.resize(length);
      if (FLAGS_profile) gtagger.set_profile(model_.dtagger_profile);
      for (int i = length - 1; i >= 0; --i) {
        // Set gradient.
        gtagger.Set(model_.dlogits, &l, i);

        // Set reference to primal cell.
        gtagger.Set(model_.primal, forward[i]);

        // Set up channels.
        if (i == length - 1) {
          gtagger.Set(model_.dh_out, &h_zero);
          gtagger.Set(model_.dc_out, &c_zero);
        } else {
          gtagger.Set(model_.dh_out, &dh, i + 1);
          gtagger.Set(model_.dc_out, &dc, i + 1);
        }
        gtagger.Set(model_.dh_in, &dh, i);
        gtagger.Set(model_.dc_in, &dc, i);

        // Compute backward.
        gtagger.Compute();
      }

      // Clear data.
      for (auto *d : forward) delete d;
      forward.clear();

      // Apply gradients to model.
      if (iteration % FLAGS_batch == 0) {
        if (FLAGS_lock) mu_.Lock();
        optimizer_.set_alpha(alpha_);
        optimizer_.Apply(gradients);
        loss_sum_ += local_loss_sum;
        loss_count_ += local_loss_count;
        if (FLAGS_lock) mu_.Unlock();

        gtagger.Clear();
        local_loss_sum = 0;
        local_loss_count = 0;
      }

      // Evaluate model.
      MutexLock lock(&mu_);
      num_tokens_ += length;

      // Decay learning rate.
      if (epoch_ % FLAGS_alpha_update == 0) {
        alpha_ *= FLAGS_decay;
      }

      // Report progress.
      if (epoch_ % FLAGS_report == 0) {
        double end = WallTime();
        float avg_loss = loss_sum_ / loss_count_;
        float acc = FLAGS_heldout ? Evaluate(&dev_) : exp(-avg_loss) * 100.0;
        float secs = end - start_;
        int tps = (num_tokens_ - prev_tokens_) / secs;
        LOG(INFO) << "epochs " << epoch_ << ", "
                  << "alpha " << alpha_ << ", "
                  << tps << " tokens/s, loss=" << avg_loss
                  << ", accuracy=" << acc;
        loss_sum_ = 0.0;
        loss_count_ = 0;
        start_ = WallTime();
        prev_tokens_ = num_tokens_;
      }

      epoch_++;
      if (epoch_ > FLAGS_epochs) break;
    }
  }

  // Finish tagger model.
  void Done() {
    if (FLAGS_profile) {
      DumpProfile(model_.tagger_profile);
      DumpProfile(model_.dtagger_profile);
      DumpProfile(loss_.profile());
      DumpProfile(optimizer_.profile());
    }
  }

  // Evaulate model on corpus returning accuracy.
  float Evaluate(Corpus *corpus) {
    // Create tagger instance with channels.
    Instance test(model_.tagger);
    Channel h_zero(model_.h_cnx);
    Channel c_zero(model_.c_cnx);
    h_zero.resize(1);
    c_zero.resize(1);
    Channel h(model_.h_cnx);
    Channel c(model_.c_cnx);
    if (FLAGS_profile) test.set_profile(model_.tagger_profile);

    // Run tagger on corpus and compare with gold tags.
    int num_correct = 0;
    int num_wrong = 0;
    for (Sentence *s : corpus->sentences) {
      int length = s->size();
      h.resize(length + 1);
      c.resize(length + 1);
      for (int i = 0; i < length; ++i) {
        // Set up channels.
        if (i == 0) {
          test.Set(model_.h_in, &h_zero);
          test.Set(model_.c_in, &c_zero);
        } else {
          test.Set(model_.h_in, &h, i);
          test.Set(model_.c_in, &c, i);
        }
        test.Set(model_.h_out, &h, i + 1);
        test.Set(model_.c_out, &c, i + 1);

        // Set up features.
        int word = (*s)[i].word;
        *test.Get<int>(model_.word) = word;

        // Compute forward.
        test.Compute();

        // Compute predicted tag.
        float *predictions = test.Get<float>(model_.logits);
        int best = 0;
        for (int t = 1; t < num_tags_; ++t) {
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

    // Return accuracy.
    return num_correct * 100.0 / (num_correct + num_wrong);
  }

 private:
  Dictionary words_;   // word dictionary
  Dictionary tags_;    // tag dictionary

  Corpus train_;       // training corpus
  Corpus dev_;         // test corpus

  // Model dimensions.
  int num_words_ = 0;
  int num_tags_ = 0;
  int word_dim_ = 64;
  int lstm_dim_ = 128;

  Library library_;    // kernel library
  TaggerFlow flow_;    // flow for tagger model
  Network network_;    // neural network
  ElfLinker linker_;   // linker for outputting generated code

  // Tagger model.
  TaggerModel model_;

  // Loss and optimizer.
  CrossEntropyLoss loss_;
  GradientDescentOptimizer optimizer_;

  // Statistics.
  int epoch_ = 1;
  int prev_tokens_ = 0;
  int num_tokens_ = 0;
  float loss_sum_ = 0.0;
  int loss_count_ = 0;
  float alpha_ = FLAGS_alpha;
  double start_;

  // Global lock.
  Mutex mu_;
};

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  Tagger tagger;
  tagger.ReadCorpora();
  tagger.Build();
  tagger.Compile();
  tagger.Initialize();
  tagger.Train();
  tagger.Done();

  return 0;
}


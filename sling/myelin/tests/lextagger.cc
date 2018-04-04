#include <math.h>
#include <stdio.h>
#include <string.h>
#include <condition_variable>
#include <iostream>
#include <random>
#include <string>
#include <sys/time.h>
#include <unistd.h>

#include "sling/base/clock.h"
#include "sling/base/init.h"
#include "sling/base/flags.h"
#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/file/file.h"
#include "sling/file/recordio.h"
#include "sling/frame/serialization.h"
#include "sling/frame/store.h"
#include "sling/myelin/builder.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/elf-linker.h"
#include "sling/myelin/flow.h"
#include "sling/myelin/gradient.h"
#include "sling/myelin/graph.h"
#include "sling/myelin/learning.h"
#include "sling/myelin/profile.h"
#include "sling/myelin/kernel/tensorflow.h"
#include "sling/nlp/document/document.h"
#include "sling/nlp/document/lexical-features.h"
#include "sling/nlp/document/lexicon.h"
#include "sling/util/mutex.h"
#include "sling/util/thread.h"
#include "third_party/jit/cpu.h"

const int cpu_cores = sling::jit::CPU::Processors();

DEFINE_string(train, "local/data/corpora/stanford/train.rec", "Train corpus");
DEFINE_string(dev, "local/data/corpora/stanford/dev.rec", "Test corpus");
DEFINE_bool(dump, false, "Dump flow");
DEFINE_bool(dump_cell, false, "Dump flow");
DEFINE_bool(profile, false, "Profile tagger");
DEFINE_int32(epochs, 250000, "Number of training epochs");
DEFINE_int32(report, 25000, "Report status after every n sentence");
DEFINE_double(alpha, 1.0, "Learning rate");
DEFINE_double(lambda, 0.0, "Regularization parameter");
DEFINE_double(decay, 0.5, "Learning rate decay rate");
DEFINE_double(clip, 1.0, "Gradient norm clipping");
DEFINE_int32(seed, 0, "Random number generator seed");
DEFINE_int32(alpha_update, 50000, "Number of epochs between alpha updates");
DEFINE_int32(batch, 64, "Number of epochs between gradient updates");
DEFINE_bool(shuffle, true, "Shuffle training corpus");
DEFINE_bool(heldout, true, "Test tagger on heldout data");
DEFINE_int32(threads, cpu_cores, "Number of threads for training");
DEFINE_int32(rampup, 10, "Number of seconds between thread starts");
DEFINE_bool(lock, true, "Locked gradient updates");
DEFINE_int32(lexthres, 0, "Lexicon threshold");

using namespace sling;
using namespace sling::myelin;
using namespace sling::nlp;

int64 flops_counter = 0;

double WallTime() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// POS tagger flow.
struct TaggerFlow : Flow {
  void Build(Library &library, Flow::Variable *features,
             int lstm_dim, int num_tags) {
    // Build forward flow.
    FlowBuilder tf(this, "tagger");
    tagger = tf.func();
    input = tf.Var("input", features->type, features->shape);
    input->ref = true;
    hidden = tf.LSTMLayer(input, lstm_dim);
    logits = tf.FFLayer(hidden, num_tags, true);

    // Build gradient for tagger.
    dtagger = Gradient(this, tagger, library);
    dlogits = Var("gradients/tagger/d_logits");
    dlogits->ref = true;
    dinput = Var("gradients/tagger/d_input");
  }

  Function *tagger;
  Variable *input;
  Variable *hidden;
  Variable *logits;

  Function *dtagger;
  Variable *dlogits;
  Variable *dinput;
};

// POS tagger model.
struct TaggerModel {
  ~TaggerModel() {
    delete h_zero;
    delete c_zero;
  }

  void Initialize(Network &net) {
    tagger = net.GetCell("tagger");
    h_in = net.GetParameter("tagger/h_in");
    h_out = net.GetParameter("tagger/h_out");
    c_in = net.GetParameter("tagger/c_in");
    c_out = net.GetParameter("tagger/c_out");
    input = net.GetParameter("tagger/input");
    logits = net.GetParameter("tagger/logits");

    dtagger = net.GetCell("gradients/tagger");
    primal = net.GetParameter("gradients/tagger/primal");
    dh_in = net.GetParameter("gradients/tagger/d_h_in");
    dh_out = net.GetParameter("gradients/tagger/d_h_out");
    dc_in = net.GetParameter("gradients/tagger/d_c_in");
    dc_out = net.GetParameter("gradients/tagger/d_c_out");
    dlogits = net.GetParameter("gradients/tagger/d_logits");
    dinput = net.GetParameter("gradients/tagger/d_input");

    h_zero = new Channel(h_in);
    c_zero = new Channel(c_in);
    h_zero->resize(1);
    c_zero->resize(1);
  }

  // Forward parameters.
  Cell *tagger;
  Tensor *h_in;
  Tensor *h_out;
  Tensor *c_in;
  Tensor *c_out;
  Tensor *input;
  Tensor *logits;

  // Backward parameters.
  Cell *dtagger;
  Tensor *primal;
  Tensor *dh_in;
  Tensor *dh_out;
  Tensor *dc_in;
  Tensor *dc_out;
  Tensor *dlogits;
  Tensor *dinput;

  // Zero channels.
  Channel *h_zero = nullptr;
  Channel *c_zero = nullptr;
};

// POS tagger.
class Tagger {
 public:
  typedef std::vector<Document *> Corpus;

  Tagger() {
    // Bind symbol names.
    names_ = new DocumentNames(&store_);
    names_->Bind(&store_);
    n_pos_ = store_.Lookup("/s/token/pos");

    // Set up kernel library.
    RegisterTensorflowLibrary(&library_);

    net_.set_linker(&linker_);
    if (FLAGS_profile) {
      net_.options().profiling = true;
      net_.options().global_profiler = true;
    }
    net_.options().flops_address = &flops_counter;

    // Set up feature spec.
    spec_.lexicon.normalize_digits = true;
  }

  ~Tagger() {
    for (auto *s : train_) delete s;
    for (auto *s : dev_) delete s;
    names_->Release();
  }

  // Read corpus from file.
  void ReadCorpus(const string &filename, Corpus *corpus) {
    RecordFileOptions options;
    RecordReader input(filename, options);
    Record record;
    while (input.Read(&record).ok()) {
      StringDecoder decoder(&store_, record.value.data(), record.value.size());
      Document *document = new Document(decoder.Decode().AsFrame(), names_);
      corpus->push_back(document);
      for (auto &t : document->tokens()) {
        FrameDatum *datum = store_.GetFrame(t.handle());
        Handle tag = datum->get(n_pos_);
        auto f = tagmap_.find(tag);
        if (f == tagmap_.end()) {
          int index = tagmap_.size();
          tagmap_[tag] = index;
        }
      }
    }
  }

  // Read training and test corpora.
  void ReadCorpora() {
    // Read documents.
    ReadCorpus(FLAGS_train, &train_);
    ReadCorpus(FLAGS_dev, &dev_);

    // Build lexicon.
    std::unordered_map<string, int> words;
    for (Document *s : train_) {
      for (const Token &t : s->tokens()) words[t.text()]++;
    }
    Vocabulary::HashMapIterator wordit(words);
    lex_.InitializeLexicon(&wordit, spec_.lexicon);

    num_words_ = lex_.lexicon().size();
    num_tags_ = tagmap_.size();

    LOG(INFO) << "Train sentences: " << train_.size();
    LOG(INFO) << "Dev sentences: " << dev_.size();
    LOG(INFO) << "Words: " << num_words_;
    LOG(INFO) << "Tags: " << num_tags_;
  }

  // Build tagger flow.
  void Build() {
    // Build feature input mapping.
    auto *fv = lex_.Build(library_, spec_, &flow_, true);

    // Build flow for POS tagger.
    flow_.Build(library_, fv, lstm_dim_, num_tags_);
    flow_.AddConnector("features")->AddLink(fv).AddLink(flow_.input);
    auto *dfv = flow_.Var("gradients/features/d_feature_vector");
    flow_.AddConnector("dfeatures")->AddLink(dfv).AddLink(flow_.dinput);

    // Build loss computation.
    loss_.Build(&flow_, flow_.logits, flow_.dlogits);

    // Build optimizer.
    optimizer_.set_clipping_threshold(FLAGS_clip);
    optimizer_.set_lambda(FLAGS_lambda);
    optimizer_.Build(&flow_);
  }

  // Compile model.
  void Compile() {
    // Output raw DOT graph.
    GraphOptions opts;
    FlowToDotGraphFile(flow_, opts, "/tmp/postagger-raw.dot");

    // Analyze flow.
    Clock analyze_clock;
    analyze_clock.start();
    flow_.Analyze(library_);
    analyze_clock.stop();

    // Dump flow.
    if (FLAGS_dump) {
      std::cout << flow_.ToString();
    }

    // Output DOT graph.
    FlowToDotGraphFile(flow_, opts, "/tmp/postagger.dot");

    // Compile network.
    Clock compile_clock;
    compile_clock.start();
    CHECK(net_.Compile(flow_, library_));
    compile_clock.stop();
    LOG(INFO) << "Analyze: " << analyze_clock.ms() << " ms, compile: "
              << compile_clock.ms() << " ms";

    // Dump cells.
    if (FLAGS_dump_cell) {
      for (Cell *cell : net_.cells()) std::cout << cell->ToString();
    }

    // Write object file with generated code.
    linker_.Link();
    linker_.Write("/tmp/postagger.o");

    // Initialize model.
    lex_.Initialize(net_);
    model_.Initialize(net_);
    loss_.Initialize(net_);
    optimizer_.Initialize(net_);
  }

  // Initialize model weights.
  void Initialize() {
    std::mt19937 prng(FLAGS_seed);
    std::normal_distribution<float> normal(0.0, 1e-4);
    for (Tensor *tensor : net_.globals()) {
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
    pool.Start(FLAGS_threads, [this](int index) { Worker(index); });

    // Evaluate model at regular intervals.
    for (;;) {
      // Wait for next eval.
      {
        std::unique_lock<std::mutex> lock(eval_mu_);
        eval_model_.wait(lock);
      }

      // Evaluate model.
      float avg_loss = loss_sum_ / loss_count_;
      loss_sum_ = 0.0;
      loss_count_ = 0;
      float acc = FLAGS_heldout ? Evaluate(&dev_) : exp(-avg_loss) * 100.0;

      double end = WallTime();
      float secs = end - start_;
      int tps = (num_tokens_ - prev_tokens_) / secs;
      int64 flops = flops_counter - prev_flops_;
      float gflops = flops / secs / 1e9;

      LOG(INFO) << "epochs " << epoch_ << ", "
                << "alpha " << alpha_ << ", "
                << num_workers_ << " workers, "
                << tps << " tokens/s, "
                << gflops << " GFLOPS, "
                << "loss=" << avg_loss
                << ", accuracy=" << acc;

      prev_tokens_ = num_tokens_;
      prev_flops_ = flops_counter;
      start_ = WallTime();

      // Check is we are done.
      if (epoch_ >= FLAGS_epochs) break;
    }

    // Wait until workers completes.
    pool.Join();
  }

  // Trainer worker thread.
  void Worker(int index) {
    // Ramp-up peiod.
    sleep(index * FLAGS_rampup);
    num_workers_++;

    // Lexical feature learner.
    DocumentFeatures features(&lex_.lexicon());
    LexicalFeatureLearner extractor(lex_);

    // Create channels.
    Channel h(model_.h_in);
    Channel c(model_.c_in);
    Channel l(model_.dlogits);
    Channel dh(model_.dh_in);
    Channel dc(model_.dh_out);
    Channel dfv(model_.dinput);

    // Allocate gradients.
    Instance gtagger(model_.dtagger);
    std::vector<Instance *> gradients = {extractor.gradient(), &gtagger};

    std::mt19937 prng(FLAGS_seed + index);
    std::uniform_real_distribution<float> rndprob(0.0, 1.0);
    int num_sentences = train_.size();
    int iteration = 0;
    float local_loss_sum = 0.0;
    int local_loss_count = 0;
    int local_tokens = 0;
    while (true) {
      // Select next sentence to train on.
      int sample = (FLAGS_shuffle ? prng() : iteration) % num_sentences;
      Document *sentence = train_[sample];
      iteration++;

      // Initialize training on sentence instance.
      int length = sentence->num_tokens();
      h.resize(length + 1);
      c.resize(length + 1);
      l.resize(length);

      // Extract features from sentence and map through embeddings.
      features.Extract(*sentence);
      Channel *fv = extractor.Extract(features, 0, length);

      // Forward pass.
      std::vector<Instance *> forward;
      for (int i = 0; i < length; ++i) {
        // Create new instance for token.
        Instance *data = new Instance(model_.tagger);
        forward.push_back(data);

        // Set up channels.
        if (i == 0) {
          data->Set(model_.h_in, model_.h_zero);
          data->Set(model_.c_in, model_.c_zero);
        } else {
          data->Set(model_.h_in, &h, i);
          data->Set(model_.c_in, &c, i);
        }
        data->Set(model_.h_out, &h, i + 1);
        data->Set(model_.c_out, &c, i + 1);

        // Set up features.
        data->Set(model_.input, fv, i);

        // Compute forward.
        data->Compute();

        // Compute loss and gradient.
        int target = Tag(sentence->token(i));
        float *logits = data->Get<float>(model_.logits);
        float *dlogits = reinterpret_cast<float *>(l.at(i));
        float cost = loss_.Compute(logits, target, dlogits);
        local_loss_sum += cost;
        local_loss_count++;
      }

      // Backpropagate loss gradient.
      dh.resize(length);
      dc.resize(length);
      dfv.resize(length);
      for (int i = length - 1; i >= 0; --i) {
        // Set gradient.
        gtagger.Set(model_.dlogits, &l, i);

        // Set reference to primal cell.
        gtagger.Set(model_.primal, forward[i]);

        // Set up channels.
        if (i == length - 1) {
          gtagger.Set(model_.dh_out, model_.h_zero);
          gtagger.Set(model_.dc_out, model_.c_zero);
        } else {
          gtagger.Set(model_.dh_out, &dh, i + 1);
          gtagger.Set(model_.dc_out, &dc, i + 1);
        }
        gtagger.Set(model_.dh_in, &dh, i);
        gtagger.Set(model_.dc_in, &dc, i);

        // Set feature vector gradient.
        gtagger.Set(model_.dinput, &dfv, i);

        // Compute backward.
        gtagger.Compute();
      }
      extractor.Backpropagate(&dfv);

      // Clear data.
      for (auto *d : forward) delete d;
      forward.clear();
      local_tokens += length;

      // Apply gradients to model.
      if (iteration % FLAGS_batch == 0) {
        if (FLAGS_lock) update_mu_.Lock();
        optimizer_.set_alpha(alpha_);
        optimizer_.Apply(gradients);
        loss_sum_ += local_loss_sum;
        loss_count_ += local_loss_count;
        num_tokens_ += local_tokens;
        if (FLAGS_lock) update_mu_.Unlock();

        gtagger.Clear();
        extractor.Clear();
        local_loss_sum = 0;
        local_loss_count = 0;
        local_tokens = 0;
      }

      // Check if new evaluation should be triggered.
      std::unique_lock<std::mutex> lock(eval_mu_);
      if (epoch_ % FLAGS_report == 0) eval_model_.notify_one();

      // Decay learning rate.
      if (epoch_ % FLAGS_alpha_update == 0) {
        alpha_ *= FLAGS_decay;
      }

      // Next epoch.
      if (epoch_ >= FLAGS_epochs) break;
      epoch_++;
    }
  }

  // Finish tagger model.
  void Done() {
    if (FLAGS_profile) {
      for (Cell *cell : net_.cells()) {
        Profile profile(cell->profile_summary());
        string report = profile.ASCIIReport();
        std::cout << report << "\n";
      }
    }
  }

  // Evaulate model on corpus returning accuracy.
  float Evaluate(Corpus *corpus) {
    // Create tagger instance with channels.
    DocumentFeatures features(&lex_.lexicon());
    LexicalFeatureExtractor extractor(lex_);
    Instance test(model_.tagger);
    Channel h(model_.h_in);
    Channel c(model_.c_in);
    Channel f(lex_.feature_vector());

    // Run tagger on corpus and compare with gold tags.
    int num_correct = 0;
    int num_wrong = 0;
    for (Document *s : *corpus) {
      int length = s->num_tokens();
      h.resize(length + 1);
      c.resize(length + 1);

      features.Extract(*s);
      extractor.Extract(features, 0, length, &f);

      for (int i = 0; i < length; ++i) {
        // Set up channels.
        if (i == 0) {
          test.Set(model_.h_in, model_.h_zero);
          test.Set(model_.c_in, model_.c_zero);
        } else {
          test.Set(model_.h_in, &h, i);
          test.Set(model_.c_in, &c, i);
        }
        test.Set(model_.h_out, &h, i + 1);
        test.Set(model_.c_out, &c, i + 1);

        // Set up features.
        test.Set(model_.input, &f, i);

        // Compute forward.
        test.Compute();

        // Compute predicted tag.
        float *predictions = test.Get<float>(model_.logits);
        int best = 0;
        for (int t = 1; t < num_tags_; ++t) {
          if (predictions[t] > predictions[best]) best = t;
        }

        // Compare with golden tag.
        int target = Tag(s->token(i));
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

  // Return tag for token.
  int Tag(const Token &token) {
    FrameDatum *datum = store_.GetFrame(token.handle());
    return tagmap_[datum->get(n_pos_)];
  }

 private:
  LexicalFeatures::Spec spec_;  // lexical feature specification
  LexicalFeatures lex_;         // lexical feature inputs
  Store store_;                 // document store
  DocumentNames *names_;        // document symbol names
  Handle n_pos_;                // part-of-speech role symbol
  HandleMap<int> tagmap_;       // mapping from tag symbol to tag id

  Corpus train_;                // training corpus
  Corpus dev_;                  // test corpus

  // Model dimensions.
  int num_words_ = 0;
  int num_tags_ = 0;
  int word_dim_ = 64;
  int lstm_dim_ = 128;

  Library library_;             // kernel library
  TaggerFlow flow_;             // flow for tagger model
  Network net_;                 // neural net
  ElfLinker linker_;            // linker for outputting generated code

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
  int num_workers_ = 0;
  int64 prev_flops_ = 0;

  // Global locks.
  Mutex update_mu_;
  Mutex eval_mu_;
  std::condition_variable eval_model_;
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


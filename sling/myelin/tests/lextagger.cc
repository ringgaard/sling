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
#include "sling/nlp/document/lexical-encoder.h"
#include "sling/nlp/document/lexicon.h"
#include "sling/util/mutex.h"
#include "sling/util/thread.h"
#include "third_party/jit/cpu.h"

const int cpu_cores = sling::jit::CPU::Processors();

DEFINE_string(train, "local/data/corpora/stanford/train.rec", "Train corpus");
DEFINE_string(dev, "local/data/corpora/stanford/dev.rec", "Test corpus");
DEFINE_string(embeddings, "", "Pre-trained word embeddings");
DEFINE_bool(dump, false, "Dump flow");
DEFINE_bool(dump_cell, false, "Dump flow");
DEFINE_bool(profile, false, "Profile tagger");
DEFINE_int32(epochs, 1000000, "Number of training epochs");
DEFINE_int32(report, 25000, "Report status after every n sentence");
DEFINE_double(alpha, 1.0, "Learning rate");
DEFINE_double(minalpha, 0.01, "Minimum learning rate");
DEFINE_double(eta, 0.0001, "Learning rate for Adam");
DEFINE_double(beta1, 0.9, "Decay rate for the first moment estimates");
DEFINE_double(beta2, 0.999, "Decay rate for the second moment estimates");
DEFINE_double(epsilon, 1e-8, "Underflow correction");
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
DEFINE_string(flow, "", "Flow file for saving trained POS tagger");
DEFINE_bool(adam, false, "Use Adam optimizer");

using namespace sling;
using namespace sling::myelin;
using namespace sling::nlp;

int64 flops_counter = 0;

double WallTime() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// POS tagger model.
struct TaggerModel {
  void Initialize(Network &net) {
    tagger = net.GetCell("tagger");
    lr = net.GetParameter("tagger/lr");
    rl = net.GetParameter("tagger/rl");
    logits = net.GetParameter("tagger/logits");

    dtagger = net.GetCell("gradients/tagger");
    primal = net.GetParameter("gradients/tagger/primal");
    dlr = net.GetParameter("gradients/tagger/d_lr");
    drl = net.GetParameter("gradients/tagger/d_rl");
    dlogits = net.GetParameter("gradients/tagger/d_logits");
  }

  // Forward parameters.
  Cell *tagger;
  Tensor *rl;
  Tensor *lr;
  Tensor *logits;

  // Backward parameters.
  Cell *dtagger;
  Tensor *primal;
  Tensor *dlr;
  Tensor *drl;
  Tensor *dlogits;
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

    spec_.lexicon.normalize_digits = true;
  }

  ~Tagger() {
    for (auto *s : train_) delete s;
    for (auto *s : dev_) delete s;
    names_->Release();
    delete optimizer_;
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

    // Align tag set size. REMOVE!!!
    tagmap_[store_.Lookup("EXTRA1")] = -1;
    tagmap_[store_.Lookup("EXTRA2")] = -1;
    tagmap_[store_.Lookup("EXTRA3")] = -1;

    num_tags_ = tagmap_.size();

    LOG(INFO) << "Train sentences: " << train_.size();
    LOG(INFO) << "Dev sentences: " << dev_.size();
    LOG(INFO) << "Tags: " << num_tags_;
  }

  // Build tagger flow.
  void BuildFlow(Flow *flow, bool learn) {
    BiLSTM::Outputs lstm;
    if (learn) {
      // Build lexicon.
      std::unordered_map<string, int> words;
      for (Document *s : train_) {
        for (const Token &t : s->tokens()) words[t.text()]++;
      }
      if (!FLAGS_embeddings.empty()) {
        for (Document *s : dev_) {
          for (const Token &t : s->tokens()) words[t.text()]++;
        }
      }
      Vocabulary::HashMapIterator vocab(words);

      // Build document input encoder.
      lstm = encoder_.Build(flow, library_, spec_, &vocab, 128, true);
    } else {
      lstm = encoder_.Build(flow, library_, spec_, nullptr, 128, false);
    }

    // Build flow for POS tagger.
    FlowBuilder tf(flow, "tagger");
    auto *tagger = tf.func();
    auto *lr = tf.Placeholder("lr", lstm.lr->type, lstm.lr->shape, true);
    auto *rl = tf.Placeholder("rl", lstm.rl->type, lstm.rl->shape, true);
    auto *logits = tf.FFLayer(tf.Concat({lr, rl}), num_tags_, true);

    flow_.Connect({lr, lstm.lr});
    flow_.Connect({rl, lstm.rl});

    if (learn) {
      // Build gradient for tagger.
      Gradient(flow, tagger, library_);
      auto *dlogits = flow->Var("gradients/tagger/d_logits");

      // Build loss computation.
      loss_.Build(flow, logits, dlogits);

      // Build optimizer.
      if (FLAGS_adam) {
        LOG(INFO) << "Using Adam optimizer";
        AdamOptimizer *adam = new AdamOptimizer();
        adam->set_alpha(FLAGS_eta);
        adam->set_beta1(FLAGS_beta1);
        adam->set_beta2(FLAGS_beta2);
        adam->set_clipping_threshold(FLAGS_clip);
        adam->set_epsilon(FLAGS_epsilon);
        optimizer_ = adam;
      } else {
        LOG(INFO) << "Using SGD optimizer";
        GradientDescentOptimizer *sgd = new GradientDescentOptimizer();
        sgd->set_lambda(FLAGS_lambda);
        sgd->set_clipping_threshold(FLAGS_clip);
        optimizer_ = sgd;
      }
      optimizer_->Build(flow);

      num_words_ = encoder_.lex().lexicon().size();
      LOG(INFO) << "Words: " << num_words_;
    }
  }

  // Build flow for learning.
  void Build() {
    BuildFlow(&flow_, true);
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
    encoder_.Initialize(net_);
    model_.Initialize(net_);
    loss_.Initialize(net_);
    optimizer_->Initialize(net_);
  }

  // Initialize model weights.
  void Initialize() {
    // Initialize parameters with small random weights.
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

    // Load pre-trained word embeddings.
    if (!FLAGS_embeddings.empty()) {
      int n = encoder_.LoadWordEmbeddings(FLAGS_embeddings);
      LOG(INFO) << "Loaded " << n << " pretrained word embeddings";
    }
  }

  // Train model.
  void Train() {
    // Start training workers.
    LOG(INFO) << "Start training";
    if (FLAGS_report > FLAGS_epochs) FLAGS_report = FLAGS_epochs;
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

    // Lexical encoder learner.
    LexicalEncoderLearner encoder(encoder_);

    // POS tagger instance.
    Instance tagger(model_.tagger);

    // Allocate gradients.
    std::vector<Instance *> gradients;
    Instance gtagger(model_.dtagger);
    encoder.CollectGradients(&gradients);
    gradients.push_back(&gtagger);

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
      int length = sentence->num_tokens();
      iteration++;

      // Run sentence through lexical encoder.
      auto lstm = encoder.Compute(*sentence, 0, length);

      // Run tagger and compute loss.
      auto grad = encoder.PrepareGradientChannels(length);
      for (int i = 0; i < length; ++i) {
        // Set hidden state from LSTMs as input to tagger.
        tagger.Set(model_.lr, lstm.lr, i);
        tagger.Set(model_.rl, lstm.rl, i);

        // Compute forward.
        tagger.Compute();

        // Compute loss and gradient.
        int target = Tag(sentence->token(i));
        float *logits = tagger.Get<float>(model_.logits);
        float *dlogits = gtagger.Get<float>(model_.dlogits);
        float loss = loss_.Compute(logits, target, dlogits);
        local_loss_sum += loss;
        local_loss_count++;

        // Backpropagate loss gradient through tagger.
        gtagger.Set(model_.primal, &tagger);
        gtagger.Set(model_.dlr, grad.lr, i);
        gtagger.Set(model_.drl, grad.rl, i);
        gtagger.Compute();
      }

      // Propagate tagger gradient through encoder.
      encoder.Backpropagate();
      local_tokens += length;

      // Apply gradients to model.
      if (iteration % FLAGS_batch == 0) {
        if (FLAGS_lock) update_mu_.Lock();
        if (!FLAGS_adam) {
          auto *sgd = static_cast<GradientDescentOptimizer *>(optimizer_);
          sgd->set_alpha(alpha_);
        }
        optimizer_->Apply(gradients);
        loss_sum_ += local_loss_sum;
        loss_count_ += local_loss_count;
        num_tokens_ += local_tokens;
        if (FLAGS_lock) update_mu_.Unlock();

        gtagger.Clear();
        encoder.Clear();
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
        if (alpha_ < FLAGS_minalpha) alpha_ = FLAGS_minalpha;
      }

      // Next epoch.
      if (epoch_ >= FLAGS_epochs) break;
      epoch_++;
    }
  }

  // Finish tagger model.
  void Done() {
    // Output profiling information.
    if (FLAGS_profile) {
      for (Cell *cell : net_.cells()) {
        Profile profile(cell->profile_summary());
        string report = profile.ASCIIReport();
        std::cout << report << "\n";
      }
    }

    // Save trained model.
    if (!FLAGS_flow.empty()) {
      LOG(INFO) << "Saving model to " << FLAGS_flow;
      Flow flow;
      BuildFlow(&flow, false);
      net_.SaveLearnedWeights(&flow);
      encoder_.SaveLexicon(&flow);
      flow.Save(FLAGS_flow);
    }
  }

  // Evaulate model on corpus returning accuracy.
  float Evaluate(Corpus *corpus) {
    // Create tagger instance with channels.
    LexicalEncoderInstance encoder(encoder_);
    Instance tagger(model_.tagger);

    // Run tagger on corpus and compare with gold tags.
    int num_correct = 0;
    int num_wrong = 0;
    for (Document *s : *corpus) {
      int length = s->num_tokens();
      auto lstm = encoder.Compute(*s, 0, length);
      for (int i = 0; i < length; ++i) {
        // Set up inputs from LSTMs.
        tagger.Set(model_.lr, lstm.lr, i);
        tagger.Set(model_.rl, lstm.rl, i);

        // Compute forward.
        tagger.Compute();

        // Compute predicted tag.
        float *predictions = tagger.Get<float>(model_.logits);
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
  LexicalFeatures::Spec spec_;  // feature specification
  Store store_;                 // document store
  DocumentNames *names_;        // document symbol names
  Handle n_pos_;                // part-of-speech role symbol
  HandleMap<int> tagmap_;       // mapping from tag symbol to tag id

  Corpus train_;                // training corpus
  Corpus dev_;                  // test corpus

  // Model dimensions.
  int num_words_ = 0;
  int num_tags_ = 0;

  Library library_;             // kernel library
  Flow flow_;                   // flow for tagger model
  Network net_;                 // neural net
  ElfLinker linker_;            // linker for outputting generated code

  // Document input encoder.
  LexicalEncoder encoder_;

  // Tagger model.
  TaggerModel model_;

  // Loss and optimizer.
  CrossEntropyLoss loss_;
  Optimizer *optimizer_= nullptr;

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


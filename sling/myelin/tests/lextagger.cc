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
#include "sling/myelin/compiler.h"
#include "sling/myelin/gradient.h"
#include "sling/myelin/learning.h"
#include "sling/myelin/profile.h"
#include "sling/myelin/rnn.h"
#include "sling/nlp/document/document.h"
#include "sling/nlp/document/lexical-features.h"
#include "sling/nlp/document/lexicon.h"
#include "sling/util/mutex.h"
#include "sling/util/thread.h"
#include "third_party/jit/cpu.h"

using namespace sling;
using namespace sling::myelin;
using namespace sling::nlp;

const int cpu_cores = jit::CPU::Processors();

DEFINE_string(train, "local/data/corpora/stanford/train.rec", "Train corpus");
DEFINE_string(dev, "local/data/corpora/stanford/dev.rec", "Test corpus");
DEFINE_string(embeddings, "", "Pre-trained word embeddings");
DEFINE_bool(train_embeddings, true, "Train word embeddings jointly");
DEFINE_int32(epochs, 1000000, "Number of training epochs");
DEFINE_int32(report, 25000, "Report status after every n sentence");
DEFINE_double(alpha, 1.0, "Learning rate");
DEFINE_double(minalpha, 0.01, "Minimum learning rate");
DEFINE_double(eta, 0.0001, "Learning rate for Adam");
DEFINE_double(beta1, 0.9, "Decay rate for the first moment estimates");
DEFINE_double(beta2, 0.999, "Decay rate for the second moment estimates");
DEFINE_double(epsilon, 1e-8, "Underflow correction");
DEFINE_double(lambda, 0.0, "Regularization parameter");
DEFINE_double(gamma, 0.6, "Momentum rate");
DEFINE_double(decay, 0.5, "Learning rate decay rate");
DEFINE_double(clip, 1.0, "Gradient norm clipping");
DEFINE_int64(seed, 0, "Random number generator seed");
DEFINE_int32(batch, 64, "Number of epochs between gradient updates");
DEFINE_bool(shuffle, true, "Shuffle training corpus");
DEFINE_bool(heldout, true, "Test tagger on heldout data");
DEFINE_int32(threads, cpu_cores, "Number of threads for training");
DEFINE_int32(rampup, 10, "Number of seconds between thread starts");
DEFINE_bool(lock, true, "Locked gradient updates");
DEFINE_int32(lexthres, 0, "Lexicon threshold");
DEFINE_int32(worddim, 32, "Word embedding dimensions");
DEFINE_int32(rnn_type, RNN::DRAGNN_LSTM, "RNN type");
DEFINE_int32(rnn_dim, 128, "RNN size");
DEFINE_int32(rnn_layers, 1, "RNN layers");
DEFINE_bool(rnn_bidir, true, "Bidirectional RNNs");
DEFINE_string(flow, "", "Flow file for saving trained POS tagger");
DEFINE_bool(adam, false, "Use Adam optimizer");
DEFINE_bool(momentum, false, "Use Momentum optimizer");
DEFINE_bool(optacc, false, "Decay learning rate based on accuracy");
DEFINE_string(normalization, "d", "Token normalization");
DEFINE_int32(tagset_align, 1, "Tag set size alignment");

int64 flops_counter = 0;

// A lexical encoder is a lexical feature extractor with an RNN on top.
class LexicalEncoder {
 public:
  LexicalEncoder(const string &lexname = "features",
                 const string &rnnname = "encoder")
      : lex_(lexname), rnn_(rnnname) {}

  // Add RNN layers to encoder.
  void AddLayers(int layers, const myelin::RNN::Spec spec, bool bidir) {
    rnn_.AddLayers(layers, spec, bidir);
  }

  // Build flow for lexical encoder. Returns the output variables from the RNN.
  myelin::RNN::Variables Build(myelin::Flow *flow,
                               const LexicalFeatures::Spec &spec,
                               Vocabulary::Iterator *words, bool learn);

  // Initialize feature extractor from existing model.
  void Initialize(const myelin::Network &net);

  // Lexical features module.
  const LexicalFeatures &lex() const { return lex_; }

  // Save lexicon.
  void SaveLexicon(myelin::Flow *flow) const { lex_.SaveLexicon(flow); }

  // Load lexicon.
  void LoadLexicon(myelin::Flow *flow) { lex_.LoadLexicon(flow); }

 private:
  // Lexical feature extractor with embeddings.
  LexicalFeatures lex_;

  // RNN encoder.
  myelin::RNNStack rnn_;

  friend class LexicalEncoderPredictor;
  friend class LexicalEncoderLearner;
};

// Lexical encoder instance.
class LexicalEncoderPredictor {
 public:
  LexicalEncoderPredictor(const LexicalEncoder &encoder)
    : encoder_(encoder),
      features_(encoder_.lex_),
      rnn_(encoder_.rnn_),
      fv_(encoder.lex().feature_vector()) {}

  // Extract lexical features from a range of tokens in a document, map the
  // features through the feature embeddings, and run the RNN encoder. Returns
  // the channel for the hidden state of the RNN.
  myelin::Channel *Compute(const Document &document, int begin, int end);

 private:
  const LexicalEncoder &encoder_;
  LexicalFeatureExtractor features_;
  myelin::RNNStackPredictor rnn_;
  myelin::Channel fv_;
};

// Lexical encoder learner.
class LexicalEncoderLearner {
 public:
  LexicalEncoderLearner(const LexicalEncoder &encoder)
      : encoder_(encoder),
        features_(encoder.lex_),
        rnn_(encoder_.rnn_) {}

  // Compute hidden state for the RNN from input document.
  myelin::Channel *Compute(const Document &document, int begin, int end);

  // Backpropagate hidden state gradients.
  void Backpropagate(myelin::Channel *doutput);

  // Collect gradients.
  void CollectGradients(Instances *gradients) {
    features_.CollectGradients(gradients);
    rnn_.CollectGradients(gradients);
  }

 private:
  const LexicalEncoder &encoder_;
  LexicalFeatureLearner features_;
  myelin::RNNStackLearner rnn_;
};

RNN::Variables LexicalEncoder::Build(Flow *flow,
                                     const LexicalFeatures::Spec &spec,
                                     Vocabulary::Iterator *words,
                                     bool learn) {
  if (words != nullptr) {
    lex_.InitializeLexicon(words, spec.lexicon);
  }
  auto lexvars = lex_.Build(flow, spec, learn);
  return rnn_.Build(flow, lexvars.fv, lexvars.dfv);
}

void LexicalEncoder::Initialize(const Network &net) {
  lex_.Initialize(net);
  rnn_.Initialize(net);
}

Channel *LexicalEncoderPredictor::Compute(const Document &document,
                                         int begin, int end) {
  // Extract features and map through feature embeddings.
  features_.Extract(document, begin, end, &fv_);

  // Compute hidden states for RNN.
  return rnn_.Compute(&fv_);
}

Channel *LexicalEncoderLearner::Compute(const Document &document,
                                        int begin, int end) {
  // Extract features and map through feature embeddings.
  Channel *fv = features_.Extract(document, begin, end);

  // Compute hidden states for RNN.
  return rnn_.Compute(fv);
}

void LexicalEncoderLearner::Backpropagate(Channel *doutput) {
  // Backpropagate hidden state gradients through RNN.
  Channel *dfv = rnn_.Backpropagate(doutput);

  // Backpropagate feature vector gradients to feature embeddings.
  features_.Backpropagate(dfv);
}

double WallTime() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// POS tagger model.
struct TaggerModel {
  void Initialize(Network &net) {
    tagger = net.GetCell("tagger");
    encoding = net.GetParameter("tagger/encoding");
    logits = net.GetParameter("tagger/logits");

    dtagger = net.GetCell("gradients/tagger");
    primal = net.GetParameter("gradients/tagger/primal");
    dencoding = net.GetParameter("gradients/tagger/d_encoding");
    dlogits = net.GetParameter("gradients/tagger/d_logits");
  }

  // Forward parameters.
  Cell *tagger;
  Tensor *encoding;
  Tensor *logits;

  // Backward parameters.
  Cell *dtagger;
  Tensor *primal;
  Tensor *dencoding;
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
    //n_pos_ = store_.Lookup("/s/token/pos");
    n_pos_ = store_.Lookup("postag");

    // Set FLOP counter.
    net_.options().flops_address = &flops_counter;

    // Set up lexical encoder spec.
    spec_.lexicon.normalization = ParseNormalization(FLAGS_normalization);
    spec_.word_dim = FLAGS_worddim;
    spec_.word_embeddings = FLAGS_embeddings;
    spec_.train_word_embeddings = FLAGS_train_embeddings;
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
    while (!input.Done()) {
      CHECK(input.Read(&record));
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

    // Align tag set size.
    while (tagmap_.size() % FLAGS_tagset_align != 0) {
      string tagname = "TAG" + std::to_string(tagmap_.size());
      tagmap_[store_.Lookup(tagname)] = -1;
    }

    num_tags_ = tagmap_.size();

    LOG(INFO) << "Train sentences: " << train_.size();
    LOG(INFO) << "Dev sentences: " << dev_.size();
    LOG(INFO) << "Tags: " << num_tags_;
  }

  // Build tagger flow.
  void BuildFlow(Flow *flow, bool learn) {
    // Set up RNNs.
    RNN::Spec rnn_spec;
    rnn_spec.type = static_cast<RNN::Type>(FLAGS_rnn_type);
    rnn_spec.dim = FLAGS_rnn_dim;
    encoder_.AddLayers(FLAGS_rnn_layers, rnn_spec, FLAGS_rnn_bidir);

    RNN::Variables rnn;
    if (learn) {
      // Build lexicon.
      std::unordered_map<string, int> words;
      for (Document *s : train_) {
        for (const Token &t : s->tokens()) words[t.word()]++;
      }
      if (!FLAGS_embeddings.empty()) {
        for (Document *s : dev_) {
          for (const Token &t : s->tokens()) words[t.word()]++;
        }
      }
      Vocabulary::HashMapIterator vocab(words);

      // Build document input encoder.
      rnn = encoder_.Build(flow, spec_, &vocab, true);
    } else {
      rnn = encoder_.Build(flow, spec_, nullptr, false);
    }

    // Build flow for POS tagger.
    FlowBuilder tf(flow, "tagger");
    auto *tagger = tf.func();
    auto *encoding = tf.Placeholder("encoding",
                                    rnn.output->type, rnn.output->shape,
                                    true);
    auto *logits = tf.FFLayer(encoding, num_tags_, true);
    flow_.Connect({encoding, rnn.output});

    if (learn) {
      // Build gradient for tagger.
      Gradient(flow, tagger);
      auto *dlogits = flow->GradientVar(logits);

      // Build loss computation.
      loss_.Build(flow, logits, dlogits);

      // Build optimizer.
      if (FLAGS_adam) {
        LOG(INFO) << "Using Adam optimizer";
        AdamOptimizer *adam = new AdamOptimizer();
        adam->set_learning_rate(FLAGS_eta);
        adam->set_decay(FLAGS_decay);
        adam->set_beta1(FLAGS_beta1);
        adam->set_beta2(FLAGS_beta2);
        adam->set_clipping_threshold(FLAGS_clip);
        adam->set_epsilon(FLAGS_epsilon);
        optimizer_ = adam;
        alpha_ = FLAGS_eta;
      } else if (FLAGS_momentum) {
        LOG(INFO) << "Using Momentum optimizer";
        MomentumOptimizer *momentum = new MomentumOptimizer();
        momentum->set_learning_rate(FLAGS_alpha);
        momentum->set_decay(FLAGS_decay);
        momentum->set_momentum(FLAGS_gamma);
        momentum->set_clipping_threshold(FLAGS_clip);
        optimizer_ = momentum;
        alpha_ = FLAGS_alpha;
      } else {
        LOG(INFO) << "Using SGD optimizer";
        GradientDescentOptimizer *sgd = new GradientDescentOptimizer();
        sgd->set_learning_rate(FLAGS_alpha);
        sgd->set_decay(FLAGS_decay);
        sgd->set_lambda(FLAGS_lambda);
        sgd->set_clipping_threshold(FLAGS_clip);
        optimizer_ = sgd;
        alpha_ = FLAGS_alpha;
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
    // Compile flow.
    compiler_.Compile(&flow_, &net_);

    // Initialize model.
    encoder_.Initialize(net_);
    model_.Initialize(net_);
    loss_.Initialize(net_);
    optimizer_->Initialize(net_);
  }

  // Initialize model weights.
  void Initialize() {
    // Initialize parameters with Gaussian noise.
    net_.InitModelParameters(FLAGS_seed);
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
      float loss = loss_sum_ / loss_count_;
      loss_sum_ = 0.0;
      loss_count_ = 0;
      float acc = FLAGS_heldout ? Evaluate(&dev_) : exp(-loss) * 100.0;

      double end = WallTime();
      float secs = end - start_;
      int tps = (num_tokens_ - prev_tokens_) / secs;
      int64 flops = flops_counter - prev_flops_;
      int gflops = flops / secs / 1e9;

      LOG(INFO) << "epochs " << epoch_ << ", "
                << "alpha " << alpha_ << ", "
                << num_workers_ << " workers, "
                << tps << " tokens/s, "
                << gflops << " GFLOPS, "
                << "loss=" << loss
                << ", accuracy=" << acc;

      prev_tokens_ = num_tokens_;
      prev_flops_ = flops_counter;
      start_ = WallTime();

      // Decay learning rate if loss increases or accuracy drops.
      bool decay = false;
      if (FLAGS_optacc) {
        if (acc < prev_acc_ && prev_acc_ != 0.0) decay = true;
      } else {
        if (loss > prev_loss_ && prev_loss_ != 0.0) decay = true;
      }
      if (decay) {
        alpha_ = optimizer_->DecayLearningRate();
      }
      prev_loss_ = loss;
      prev_acc_ = acc;

      // Check is we are done.
      if (epoch_ >= FLAGS_epochs) break;
    }

    // Wait until workers completes.
    pool.Join();
  }

  // Trainer worker thread.
  void Worker(int index) {
    // Ramp-up period.
    sleep(index * FLAGS_rampup);
    num_workers_++;

    // Lexical encoder learner.
    LexicalEncoderLearner encoder(encoder_);

    // POS tagger instance.
    Instance tagger(model_.tagger);
    Channel grad(model_.dencoding);

    // Allocate gradients.
    Instances gradients;
    Instance gtagger(model_.dtagger);
    encoder.CollectGradients(&gradients);
    gradients.Add(&gtagger);

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
      Channel *encodings = encoder.Compute(*sentence, 0, length);

      // Run tagger and compute loss.
      grad.reset(length);
      for (int i = 0; i < length; ++i) {
        // Set hidden state from RNN as input to tagger.
        tagger.Set(model_.encoding, encodings, i);

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
        gtagger.Set(model_.dencoding, &grad, i);
        gtagger.Compute();
      }

      // Propagate tagger gradient through encoder.
      encoder.Backpropagate(&grad);
      local_tokens += length;

      // Apply gradients to model.
      if (iteration % FLAGS_batch == 0) {
        if (FLAGS_lock) update_mu_.Lock();
        optimizer_->Apply(gradients);
        loss_sum_ += local_loss_sum;
        loss_count_ += local_loss_count;
        num_tokens_ += local_tokens;
        if (FLAGS_lock) update_mu_.Unlock();

        gradients.Clear();
        local_loss_sum = 0;
        local_loss_count = 0;
        local_tokens = 0;
      }

      // Check if new evaluation should be triggered.
      std::unique_lock<std::mutex> lock(eval_mu_);
      if (epoch_ % FLAGS_report == 0) eval_model_.notify_one();

      // Next epoch.
      if (epoch_ >= FLAGS_epochs) break;
      epoch_++;
    }
  }

  // Finish tagger model.
  void Done() {
    // Output profiling information.
    LogProfile(net_);

    // Save trained model.
    if (!FLAGS_flow.empty()) {
      LOG(INFO) << "Saving model to " << FLAGS_flow;
      Flow flow;
      BuildFlow(&flow, false);
      net_.SaveParameters(&flow);
      encoder_.SaveLexicon(&flow);
      flow.Save(FLAGS_flow);
    }
  }

  // Evaulate model on corpus returning accuracy.
  float Evaluate(Corpus *corpus) {
    // Create tagger instance with channels.
    LexicalEncoderPredictor encoder(encoder_);
    Instance tagger(model_.tagger);

    // Run tagger on corpus and compare with gold tags.
    int num_correct = 0;
    int num_wrong = 0;
    for (Document *s : *corpus) {
      int length = s->num_tokens();
      Channel *encodings = encoder.Compute(*s, 0, length);
      for (int i = 0; i < length; ++i) {
        // Set up input from RNN.
        tagger.Set(model_.encoding, encodings, i);

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

  // Neural network.
  Flow flow_;                   // flow for tagger model
  Network net_;                 // neural net
  Compiler compiler_;           // neural network compiler

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
  float prev_loss_ = 0.0;
  float prev_acc_ = 0.0;
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


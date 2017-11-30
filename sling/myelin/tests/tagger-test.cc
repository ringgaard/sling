#include <math.h>
#include <limits>
#include <string>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "sling/base/clock.h"
#include "sling/base/init.h"
#include "sling/base/flags.h"
#include "sling/base/logging.h"
#include "sling/file/file.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/flow.h"
#include "sling/myelin/graph.h"
#include "sling/myelin/profile.h"
#include "sling/myelin/cuda/cuda-kernel.h"
#include "sling/myelin/cuda/cuda-runtime.h"
#include "sling/myelin/kernel/cuda.h"
#include "sling/myelin/kernel/tensorflow.h"
#include "sling/myelin/kernel/dragnn.h"
#include "third_party/jit/cpu.h"

DEFINE_string(model, "local/tagger-rnn.flow", "Flow model for tagger");
DEFINE_bool(baseline, false, "Compare with baseline tagger result");
DEFINE_bool(intermediate, false, "Compare intermediate with baseline tagger");

DEFINE_int32(repeat, 1, "Number of times test is repeated");
DEFINE_bool(profile, false, "Profile computation");
DEFINE_bool(data_profile, false, "Output data instance profile");
DEFINE_bool(dynamic, false, "Dynamic instance allocation");
DEFINE_bool(dump_flow, false, "Dump analyzed flow to stdout");
DEFINE_bool(dump_cell, false, "Dump network cell to stdout");
DEFINE_bool(dump_graph, true, "Dump dot graph");
DEFINE_bool(dump_code, true, "Dump generated code");
DEFINE_bool(debug, false, "Debug mode");
DEFINE_double(epsilon, 1e-5, "Epsilon for floating point comparison");
DEFINE_bool(twisted, false, "Swap hidden and control in LSTMs");
DEFINE_bool(sync, false, "Sync all steps");
DEFINE_bool(check, true, "Check test sentence");
DEFINE_bool(fast_argmax, false, "Let network cell compute argmax");

DEFINE_bool(sse, true, "SSE support");
DEFINE_bool(sse2, true, "SSE2 support");
DEFINE_bool(sse3, true, "SSE3 support");
DEFINE_bool(sse41, true, "SSE 4.1 support");
DEFINE_bool(avx, true, "AVX support");
DEFINE_bool(avx2, true, "AVX2 support");
DEFINE_bool(fma3, true, "FMA3 support");
DEFINE_bool(gpu, false, "Run on GPU");

DEFINE_int32(strict, 0, "Strict math mode (0=relaxed,1=strict matmul,2=strict");

using namespace sling;
using namespace sling::myelin;

struct RNNInstance;

CUDARuntime cudart;

// Baseline LSTM tagger.
struct LSTMTagger {
  void Load(const string &filename) {
    // Load flow.
    CHECK(flow.Load(filename));

    // Initialize dimensions.
    vocab_size = Var("tagger/fixed_embedding_matrix_0")->dim(0);
    embed_dim = Var("tagger/fixed_embedding_matrix_0")->dim(1);
    lstm_dim = Var("tagger/h2c")->dim(0);
    output_dim = Var("tagger/bias_softmax")->dim(0);

    // Initialize parameters.
    embeddings = GetData("tagger/fixed_embedding_matrix_0");
    x2i = GetData("tagger/x2i");
    h2i = GetData("tagger/h2i");
    c2i = GetData("tagger/c2i");
    bc = GetData("tagger/bc");
    bi = GetData("tagger/bi");
    bo = GetData("tagger/bo");
    h2c = GetData("tagger/h2c");
    x2c = GetData("tagger/x2c");
    c2o = GetData("tagger/c2o");
    x2o = GetData("tagger/x2o");
    h2o = GetData("tagger/h2o");
    bo = GetData("tagger/bo");
    bias_softmax = GetData("tagger/bias_softmax");
    weights_softmax = GetData("tagger/weights_softmax");
  }

  ~LSTMTagger() { Clear(); }

  // Clear all temporary vectors.
  void Clear() {
    for (auto *v : vectors) delete [] v;
    vectors.clear();
  }

  // Get parameter data.
  const float *GetData(const string &name) {
    const float *data = reinterpret_cast<const float *>(Var(name)->data);
    CHECK(data != nullptr) << name;
    return data;
  }

  // Get flow variable.
  Flow::Variable *Var(const string &name) {
    Flow::Variable *var = flow.Var(name);
    CHECK(var != nullptr) << name;
    return var;
  }

  // Allocate new temporary vector.
  float *Vector(int size) {
    float *vec = new float[size];
    vectors.push_back(vec);
    return vec;
  }

  // Add two vectors.
  float *Add(const float *a, const float *b, int n) {
    float *c = Vector(n);
    for (int i = 0; i < n; ++i) c[i] = a[i] + b[i];
    return c;
  }

  // Subtract constant and vector.
  float *Sub(const float a, const float *b, int n) {
    float *c = Vector(n);
    for (int i = 0; i < n; ++i) c[i] = a - b[i];
    return c;
  }

  // Multiply two vectors element-wise.
  float *Mul(const float *a, const float *b, int n) {
    float *c = Vector(n);
    for (int i = 0; i < n; ++i) c[i] = a[i] * b[i];
    return c;
  }

  // Multiply vector with matrix.
  float *MatMul(const float *x, const float *w, int n, int m) {
    float *y = Vector(m);
    for (int i = 0; i < m; ++i) {
      float sum = 0.0;
      for (int j = 0; j < n; ++j) {
        sum += x[j] * w[j * m + i];
      }
      y[i] = sum;
    }
    return y;
  }

  // Compute sigmoid element-wise.
  float *Sigmoid(const float *x, int n) {
    float *y = Vector(n);
    for (int i = 0; i < n; ++i) y[i] = 1.0 / (1.0 + expf(-x[i]));
    return y;
  }

  // Compute hyperbolic tangent element-wise.
  float *Tanh(const float *x, int n) {
    float *y = Vector(n);
    for (int i = 0; i < n; ++i) y[i] = tanhf(x[i]);
    return y;
  }

  // Look up element in embedding.
  float *Lookup(const float *embedding, int index, int n) {
    float *x = Vector(n);
    for (int i = 0; i < n; ++i) x[i] = embedding[index * n + i];
    return x;
  }

  // Compute LSTM cell.
  void Compute(int word,
               float *c_in, float *h_in,
               float **c_out, float **h_out,
               float **logits) {
    // Swap control and hidden if LSTM is twisted.
    if (FLAGS_twisted) std::swap(c_in, h_in);

    // Embedding lookup.
    int index = word == -1 ? vocab_size - 1 : word;
    x = Lookup(embeddings, index, embed_dim);

    // input --  i_t = sigmoid(affine(x_t, h_{t-1}, c_{t-1}))
    // i_ait = tf.matmul(input_tensor, x2i) +
    //         tf.matmul(i_h_tm1, h2i) +
    //         tf.matmul(i_c_tm1, c2i) + bi
    i_x = MatMul(x, x2i, embed_dim, lstm_dim);
    i_h = MatMul(h_in, h2i, lstm_dim, lstm_dim);
    i_c = MatMul(c_in, c2i, lstm_dim, lstm_dim);
    i_ait = Add(Add(Add(i_c, i_x, lstm_dim), i_h, lstm_dim), bi, lstm_dim);

    // i_it = tf.sigmoid(i_ait)
    i_it = Sigmoid(i_ait, lstm_dim);

    // forget -- f_t = 1 - i_t
    i_ft = Sub(1.0, i_it, lstm_dim);

    // write memory cell -- tanh(affine(x_t, h_{t-1}))
    c_x = MatMul(x, x2c, embed_dim, lstm_dim);
    c_h = MatMul(h_in, h2c, lstm_dim, lstm_dim);
    i_awt = Add(Add(c_h, c_x, lstm_dim), bc, lstm_dim);
    i_wt = Tanh(i_awt, lstm_dim);

    // c_t = f_t \odot c_{t-1} + i_t \odot tanh(affine(x_t, h_{t-1}))
    *c_out = Add(Mul(i_it, i_wt, lstm_dim),
                 Mul(i_ft, c_in, lstm_dim),
                 lstm_dim);

    // output -- o_t = sigmoid(affine(x_t, h_{t-1}, c_t))
    o_x = MatMul(x, x2o, embed_dim, lstm_dim);
    o_c = MatMul(*c_out, c2o, lstm_dim, lstm_dim);
    o_h = MatMul(h_in, h2o, lstm_dim, lstm_dim);
    i_aot = Add(Add(Add(o_x, o_c, lstm_dim), o_h, lstm_dim), bo, lstm_dim);

    i_ot = Sigmoid(i_aot, lstm_dim);

    // ht = o_t \odot tanh(ct)
    ph_t = Tanh(*c_out, lstm_dim);
    *h_out = Mul(i_ot, ph_t, lstm_dim);

    // logits
    xw = MatMul(*h_out, weights_softmax, lstm_dim, output_dim);
    *logits = Add(xw, bias_softmax, output_dim);
  }

  // Flow with LSTM parameters.
  Flow flow;

  // LSTM dimensions.
  int vocab_size;
  int embed_dim;
  int lstm_dim;
  int output_dim;

  // LSTM parameter weights.
  const float *embeddings;
  const float *x2i, *h2i, *c2i;
  const float *bc, *bi, *bo;
  const float *h2c, *x2c;
  const float *c2o, *x2o, *h2o;
  const float *bias_softmax;
  const float *weights_softmax;

  // Intermediate results.
  float *x;
  float *i_x, *i_h, *i_c;
  float *i_ait, *i_it, *i_ft;
  float *c_x, *c_h;
  float *i_awt, *i_wt;
  float *o_x, *o_c, *o_h;
  float *i_aot, *i_ot;
  float *ph_t;
  float *xw;

  // Temporary allocated vectors.
  std::vector<float *> vectors;
};

// Compare two vectors.
bool Equals(float *a, float *b, int n, const char *name = "vector") {
  bool equal = true;
  for (int i = 0; i < n; ++i) {
    bool same = FLAGS_epsilon == 0.0 ? a[i] == b[i]
                                     : fabs(a[i] - b[i]) < FLAGS_epsilon;
    if (!same) {
      LOG(INFO) << name << "[" << i << "] a=" << a[i] << " b=" << b[i]
                << " delta=" << fabs(a[i] - b[i]);
      equal = false;
    }
  }
  return equal;
}

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

// RNN tagger.
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
  const string &tag(int index) const {
    static const string unk = "--UNK--";
    if (index < 0 || index >= tags_.size()) return unk;
    return tags_[index];
  }

  // Get tag id for tag name.
  int tagid(const string &tag) const {
    for (int i = 0; i < tags_.size(); ++i) {
      if (tag == tags_[i]) return i;
    }
    LOG(FATAL) << "Unknown tag name: " << tag;
    return -1;
  }

 private:
  // Looks up cells, connectors, and parameters.
  Cell *GetCell(const string &name);
  Connector *GetConnector(const string &name);
  Tensor *GetParam(const string &name, bool optional = false);

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
  Tensor *ff_prediction_;     // link to FF logit layer argmax

  // Lexicon.
  std::unordered_map<string, int> vocabulary_;
  int oov_ = -1;
  std::vector<string> tags_;

  // Baseline tagger.
  LSTMTagger baseline_;
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
  if (FLAGS_gpu) RegisterCUDALibrary(&library_);

  library_.Register(new FixedDragnnInitializer());
  library_.RegisterTyper(new FixedDragnnTyper());

  // Load and patch flow file.
  Flow flow;
  CHECK(flow.Load(filename));
  if (FLAGS_strict > 0) {
    for (auto *op : flow.Find({"MatMul"})) op->SetAttr("strict", true);
  }
  if (FLAGS_strict > 1) {
    for (auto *op : flow.Find({"Tanh"})) op->SetAttr("strict", true);
    for (auto *op : flow.Find({"Sigmoid"})) op->SetAttr("strict", true);
  }
  if (FLAGS_intermediate) {
    for (auto *var : flow.vars()) var->out = true;
  }

  if (FLAGS_fast_argmax) {
    auto *tagger = flow.Func("tagger");
    auto *logits = flow.Var("tagger/logits");
    auto *prediction = flow.AddVariable("tagger/prediction",
                                        myelin::DT_INT32, {1});
    flow.AddOperation(tagger, "tagger/ArgMax", "ArgMax",
                      {logits}, {prediction});
    CHECK(!logits->in);
    CHECK(!logits->out);
  }

  // Zero out the last embedding vector (used for oov).
  Flow::Variable *embedding = flow.Var("tagger/fixed_embedding_matrix_0");
  CHECK(embedding != nullptr);
  float *emb_data =
      const_cast<float *>(reinterpret_cast<const float *>(embedding->data));
  for (int i = 0; i < embedding->dim(1); i++) {
    emb_data[embedding->elements() - 1 - i] = 0.0;
  }

  // Analyze flow.
  flow.Analyze(library_);

  // Output flow.
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
  auto &opts = network_.options();
  if (FLAGS_profile) opts.profiling = true;
  if (FLAGS_debug) opts.debug = true;
  if (FLAGS_dynamic) opts.dynamic_allocation = true;
  if (FLAGS_sync) opts.sync_steps = true;
  if (FLAGS_gpu) {
    cudart.Connect();
    network_.set_runtime(&cudart);
  }

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
  ff_prediction_ = GetParam("tagger/prediction", true);

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
  if (oov_ == -1) oov_ = index - 1;

  // Load tag map.
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

  // Load baseline tagger.
  if (FLAGS_baseline) {
    baseline_.Load(filename);
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
                  std::vector<int> *predictions) {
  RNNInstance data(lr_, lr_c_, lr_h_, 0, tokens.size());

  // Look up words in vocabulary.
  for (int i = 0; i < tokens.size(); ++i) {
    int word = LookupWord(tokens[i]);
    data.words[i] = word;
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

      int prediction = 0;
      if (FLAGS_fast_argmax) {
        prediction = *data.lr.Get<int>(ff_prediction_);
      } else {
        float *output = data.lr.Get<float>(ff_output_);
        float max_score = -std::numeric_limits<float>::infinity();

        for (int a = 0; a < ff_output_->dim(1); ++a) {
          if (output[a] > max_score) {
            prediction = a;
            max_score = output[a];
          }
        }
      }
      predictions->push_back(prediction);

      // Compare with baseline.
      if (FLAGS_baseline) {
        LOG(INFO) << "Token " << i << ": " << tokens[i] << " " << data.words[i];
        float *c_in = data.Get("tagger/c_in");
        float *h_in = data.Get("tagger/h_in");
        float *c_out, *h_out, *logits;
        baseline_.Compute(data.words[i], c_in, h_in, &c_out, &h_out, &logits);

        int best = 0;
        float max_score = -std::numeric_limits<float>::infinity();
        for (int a = 0; a < ff_output_->dim(1); ++a) {
          if (logits[a] > max_score) {
            best = a;
            max_score = logits[a];
          }
        }
        if (prediction != best) {
          LOG(INFO) << "prediction: " << prediction << " baseline: " << best;
        }

        int ldim = baseline_.lstm_dim;
        int edim = baseline_.embed_dim;
        int odim = baseline_.output_dim;
        if (FLAGS_intermediate) {
          Equals(data.Get("tagger/fixed_embedding_words/Lookup"), baseline_.x,
                          edim, "x");
          Equals(data.Get("tagger/MatMul"), baseline_.i_x, ldim, "i_x");
          Equals(data.Get("tagger/MatMul_1"), baseline_.i_h, ldim, "i_h");
          Equals(data.Get("tagger/MatMul_2"), baseline_.i_c, ldim, "i_c");
          Equals(data.Get("tagger/add_2"), baseline_.i_ait, ldim, "i_ait");
          Equals(data.Get("tagger/Sigmoid"), baseline_.i_it, ldim, "i_it");
          Equals(data.Get("tagger/sub_2"), baseline_.i_ft, ldim, "i_ft");
          Equals(data.Get("tagger/Tanh"), baseline_.i_wt, ldim, "i_wt");
          Equals(data.Get("tagger/c_out"), c_out, ldim, "c_out");
          Equals(data.Get("tagger/add_7"), baseline_.i_aot, ldim, "i_aot");
          Equals(data.Get("tagger/Sigmoid_1"), baseline_.i_ot, ldim, "i_ot");
          Equals(data.Get("tagger/Tanh_1"), baseline_.ph_t, ldim, "ph_t");
          Equals(data.Get("tagger/h_out"), h_out, ldim, "h_out");
          Equals(data.Get("tagger/xw_plus_b/MatMul"), baseline_.xw, odim, "xw");
        }
        Equals(data.Get("tagger/xw_plus_b"), logits, odim, "logits");

        baseline_.Clear();
      }
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

Tensor *RNN::GetParam(const string &name, bool optional) {
  Tensor *param = network_.GetParameter(name);
  if (!optional && param == nullptr) {
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

  if (FLAGS_check) {
    CHECK_EQ(tokens.size(), tokens.size());
    for (int i = 0; i < predictions.size(); ++i) {
      CHECK_EQ(golden[i], predictions[i])
          << i << " gold: " << rnn.tag(golden[i])
          << " predicted: " << rnn.tag(predictions[i]);
    }
  }

  return 0;
}


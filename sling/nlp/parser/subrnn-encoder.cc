// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string>
#include <vector>
#include <unordered_map>

#include "sling/file/textmap.h"
#include "sling/myelin/builder.h"
#include "sling/myelin/gradient.h"
#include "sling/myelin/rnn.h"
#include "sling/nlp/document/subword-tokenizer.h"
#include "sling/nlp/document/wordpiece-builder.h"
#include "sling/nlp/parser/parser-codec.h"
#include "sling/util/unicode.h"

namespace sling {
namespace nlp {

using namespace sling::myelin;

// Token encoder using subword tokens and RNNs.
class SubwordRNNEncoder : public ParserEncoder {
 public:
  // Set up lexical RNN encoder for training.
  void Setup(task::Task *task, Store *commons) override {
    // Get word normalization.
    string normalization = task->Get("normalization", "l");
    normalization_ = ParseNormalization(normalization);

    // Initialize sub-tokenizer with subwords if present. Otherwise the subword
    // lexicons are computed from the vocabulary when the model is built.
    task->Fetch("max_subwords", &max_subwords_);
    task->Fetch("subword_dim", &subword_dim_);
    auto *subwords = task->GetInput("subwords");
    if (subwords != nullptr) {
      // Read subwords from text map file. Assume that the subwords have already
      // been normalized.
      LOG(INFO) << "Load subwords from " << subwords->filename();
      std::vector<std::pair<string, int>> vocab;
      TextMapInput input(subwords->filename());
      string word;
      int64 count;
      while (input.Read(nullptr, &word, &count)) {
        vocab.emplace_back(word, count);
      }
      Vocabulary::VectorMapIterator it(vocab);
      subtokenizer_.Init(&it);
    }

    // Set up RNNs.
    task->Fetch("rnn_dim", &rnn_dim_);
    task->Fetch("rnn_layers", &rnn_layers_);
    task->Fetch("rnn_type", &rnn_type_);
    task->Fetch("rnn_bidir", &rnn_bidir_);
    task->Fetch("rnn_highways", &rnn_highways_);

    RNN::Spec rnn_spec;
    rnn_spec.type = static_cast<RNN::Type>(rnn_type_);
    rnn_spec.dim = rnn_dim_;
    rnn_spec.highways = rnn_highways_;
    rnn_spec.dropout = task->Get("dropout", 0.0);
    rnn_.AddLayers(rnn_layers_, rnn_spec, rnn_bidir_);
  }

  // Build encoder model.
  myelin::Flow::Variable *Build(myelin::Flow *flow,
                                Vocabulary::Iterator *words,
                                bool learn) override {
    // Initialize vocabulary if not already done.
    if (words != nullptr && subtokenizer_.size() == 0) {
      // Build normalized vocabulary.
      std::unordered_map<string, int> vocab;
      Text word;
      int count;
      string normalized;
      words->Reset();
      while (words->Next(&word, &count)) {
        UTF8::Normalize(word.data(), word.size(), normalization_, &normalized);
        vocab[normalized] += count;
      }

      // Build subword lexicon.
      LOG(INFO) << "Building subword vocabulary";
      Vocabulary::HashMapIterator it(vocab);
      WordPieceBuilder wordpieces(max_subwords_);
      std::vector<string> leading;
      std::vector<string> trailing;
      wordpieces.Build(&it, [&](const WordPieceBuilder::Symbol *sym) {
        if (sym->code != -1) {
          if (sym->trailing) {
            trailing.push_back(sym->text());
          } else {
            leading.push_back(sym->text());
          }
        }
      });

      // Initialize subword tokenizer.
      Vocabulary::VectorIterator l(leading);
      Vocabulary::VectorIterator t(leading);
      subtokenizer_.Init(&l, &t);
    }

    // Build subword embeddings.
    FlowBuilder tf(flow, "subword");
    int num_subwords = subtokenizer_.size();
    auto *embeddings = tf.Parameter("embeddings", DT_FLOAT,
                                    {num_subwords, subword_dim_});
    tf.RandomNormal(embeddings);
    auto *index = tf.Placeholder("index", DT_INT32, {1, 1});
    auto *embedding = tf.Name(tf.Gather(embeddings, index), "embedding");
    embedding->set_out()->set_ref();

    // Build gradient for subword embeddings.
    Flow::Variable *dembedding = nullptr;
    if (learn) {
      Gradient(flow, tf.func());
      dembedding = flow->GradientVar(embedding);
    }

    // Build RNNs.
    auto rnnvars = rnn_.Build(flow, embedding, dembedding);
    return rnnvars.output;
  }

  // Save encoder to flow.
  void Save(Flow *flow, Builder *spec) override {
    // Save encoder spec.
    spec->Add("type", "subrnn");
    spec->Add("normalization", NormalizationString(normalization_));
    spec->Add("rnn", rnn_type_);
    spec->Add("dim", rnn_dim_);
    spec->Add("layers", rnn_layers_);
    spec->Add("bidir", rnn_bidir_);
    spec->Add("highways", rnn_highways_);

    // Save subword lexicons, i.e. leading and trailing subwords.
    Flow::Blob *leading = flow->AddBlob("leading", "dict");
    string ldata;
    subtokenizer_.WriteLeading(&ldata);
    leading->data = flow->AllocateMemory(ldata);
    leading->size = ldata.size();

    Flow::Blob *trailing = flow->AddBlob("trailing", "dict");
    string tdata;
    subtokenizer_.WriteTrailing(&tdata);
    trailing->data = flow->AllocateMemory(tdata);
    trailing->size = tdata.size();
  }

  // Load encoder from flow.
  void Load(Flow *flow, const Frame &spec) override {
    // Load subword lexicons from flow.
    normalization_ = ParseNormalization(spec.GetString("normalization"));
    Flow::Blob *leading = flow->DataBlock("leading");
    CHECK(leading != nullptr);
    Vocabulary::BufferIterator l(leading->data, leading->size);
    Flow::Blob *trailing = flow->DataBlock("trailing");
    CHECK(trailing != nullptr);
    Vocabulary::BufferIterator t(trailing->data, trailing->size);
    subtokenizer_.Init(&l, &t);

    // Set up RNN stack.
    rnn_type_ = spec.GetInt("rnn");
    rnn_dim_ = spec.GetInt("dim");
    rnn_layers_ = spec.GetInt("layers");
    rnn_bidir_ = spec.GetBool("bidir");
    rnn_highways_ = spec.GetBool("highways");

    RNN::Spec rnn_spec;
    rnn_spec.type = static_cast<myelin::RNN::Type>(rnn_type_);
    rnn_spec.dim = rnn_dim_;
    rnn_spec.highways = rnn_highways_;
    rnn_.AddLayers(rnn_layers_, rnn_spec, rnn_bidir_);
  }

  // Initialize encoder model.
  void Initialize(const myelin::Network &net) override {
    // Initialize subword embeddings.
    subword_ = net.GetCell("subword");
    subword_index_ = net.GetParameter("subword/index");
    subword_embedding_ = net.GetParameter("subword/embedding");
    gsubword_ = subword_->Gradient();
    if (gsubword_ != nullptr) {
      gsubword_primal_ = subword_->Primal();
      gsubword_dembedding_ = subword_embedding_->Gradient();
    }

    // Initialize RNNs.
    rnn_.Initialize(net);
  }

  // Encoder predictor.
  class Predictor : public ParserEncoder::Predictor {
   public:
    Predictor(const SubwordRNNEncoder *encoder)
        : rnn_(encoder->rnn_) {}
#if 0
        : features_(encoder->lex_),
          fv_(nullptr) {}
#endif

    Channel *Encode(const Document &document, int begin, int end) override {
#if 0
      // Extract features and map through feature embeddings.
      features_.Extract(document, begin, end, &fv_);

      // Compute hidden states for RNN.
      return rnn_.Compute(&fv_);
#endif
      return nullptr;
    }

   private:
    //LexicalFeatureExtractor features_;
    RNNStackPredictor rnn_;
    //Channel fv_;
  };

  Predictor *CreatePredictor() override { return new Predictor(this); }

  // Encoder learner.
  class Learner : public ParserEncoder::Learner {
   public:
    Learner(const SubwordRNNEncoder *encoder)
        : rnn_(encoder->rnn_) {}
#if 0
        : features_(encoder->lex_),
#endif

    Channel *Encode(const Document &document, int begin, int end) override {
#if 0
      // Extract features and map through feature embeddings.
      Channel *fv = features_.Extract(document, begin, end);

      // Compute hidden states for RNN.
      return rnn_.Compute(fv);
#endif
      return nullptr;
    }

    void Backpropagate(myelin::Channel *doutput) override {
#if 0
      // Backpropagate hidden state gradients through RNN.
      Channel *dfv = rnn_.Backpropagate(doutput);

      // Backpropagate feature vector gradients to feature embeddings.
      features_.Backpropagate(dfv);
#endif
    }

    void CollectGradients(Instances *gradients) override {
      //features_.CollectGradients(gradients);
      rnn_.CollectGradients(gradients);
    }

   private:
    //LexicalFeatureLearner features_;
    RNNStackLearner rnn_;
  };

  Learner *CreateLearner() override { return new Learner(this); }

 private:
  // Word normalization.
  Normalization normalization_ = NORMALIZE_NONE;

  // Maximum number of subwords.
  int max_subwords_ = 30000;

  // Dimension of subword embeddings.
  int subword_dim_ = 128;

  // Subword tokenizer.
  SubwordTokenizer subtokenizer_;

  // Tensors for subword embeddings.
  Cell *subword_ = nullptr;
  Tensor *subword_index_ = nullptr;
  Tensor *subword_embedding_ = nullptr;
  Cell *gsubword_ = nullptr;
  Tensor *gsubword_primal_ = nullptr;
  Tensor *gsubword_dembedding_ = nullptr;

  // RNN specification.
  int rnn_type_ = myelin::RNN::LSTM;
  int rnn_dim_ = 256;
  int rnn_layers_ = 1;
  bool rnn_bidir_ = true;
  bool rnn_highways_ = false;

  // RNN encoder.
  myelin::RNNStack rnn_{"encoder"};
};

REGISTER_PARSER_ENCODER("subrnn", SubwordRNNEncoder);

}  // namespace nlp
}  // namespace sling


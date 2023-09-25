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
  Flow::Variable *Build(Flow *flow,
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
      std::vector<string> subwords;
      wordpieces.Build(&it, [&](const WordPieceBuilder::Symbol *sym) {
        if (sym->code == -1) {
          subwords.push_back("[UNK]");
        } else if (sym->trailing) {
          subwords.push_back("##" + sym->text());
        } else {
          subwords.push_back(sym->text());
        }
      });

      // Initialize subword tokenizer.
      Vocabulary::VectorIterator swit(subwords);
      subtokenizer_.Init(&swit);
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

    // Save subword lexicon.
    Flow::Blob *subwords = flow->AddBlob("subwords", "dict");
    string data;
    subtokenizer_.Write(&data);
    subwords->data = flow->AllocateMemory(data);
    subwords->size = data.size();
  }

  // Load encoder from flow.
  void Load(Flow *flow, const Frame &spec) override {
    // Load subword lexicons from flow.
    normalization_ = ParseNormalization(spec.GetString("normalization"));
    Flow::Blob *subwords = flow->DataBlock("subwords");
    CHECK(subwords != nullptr);
    Vocabulary::BufferIterator it(subwords->data, subwords->size);
    subtokenizer_.Init(&it);

    // Set up RNN stack.
    rnn_type_ = spec.GetInt("rnn");
    rnn_dim_ = spec.GetInt("dim");
    rnn_layers_ = spec.GetInt("layers");
    rnn_bidir_ = spec.GetBool("bidir");
    rnn_highways_ = spec.GetBool("highways");

    RNN::Spec rnn_spec;
    rnn_spec.type = static_cast<RNN::Type>(rnn_type_);
    rnn_spec.dim = rnn_dim_;
    rnn_spec.highways = rnn_highways_;
    rnn_.AddLayers(rnn_layers_, rnn_spec, rnn_bidir_);
  }

  // Initialize encoder model.
  void Initialize(const Network &net) override {
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
    rnn_output_ = rnn_.output();
    if (rnn_output_ == nullptr) rnn_output_ = subword_embedding_;
    rnn_doutput_ = rnn_.doutput();
    if (rnn_doutput_ == nullptr) rnn_doutput_ = gsubword_dembedding_;
  }

  // Encoder predictor.
  class Predictor : public ParserEncoder::Predictor {
   public:
    Predictor(const SubwordRNNEncoder *encoder)
        : encoder_(encoder),
          subword_(encoder->subword_),
          subword_embeddings_(encoder->subword_embedding_),
          rnn_(encoder->rnn_),
          word_encodings_(encoder->rnn_output_) {}

    Channel *Encode(const Document &document, int begin, int end) override {
      // Split tokens into subwords.
      int length = end - begin;
      subword_index_.clear();
      token_start_.resize(length);
      string normalized;
      for (int t = 0; t < length; ++t) {
        token_start_[t] = subword_index_.size();
        const Token &token = document.token(t + begin);
        UTF8::Normalize(token.word(), encoder_->normalization_, &normalized);
        encoder_->subtokenizer_.Tokenize(normalized, &subword_index_);
      }

      // Look up subword embeddings.
      int num_subwords = subword_index_.size();
      subword_embeddings_.resize(num_subwords);
      for (int i = 0; i < num_subwords; ++i) {
        *subword_.Get<int>(encoder_->subword_index_) = subword_index_[i];
        subword_.Set(encoder_->subword_embedding_, &subword_embeddings_, i);
        subword_.Compute();
      }

      // Compute RNN hidden state for each subword token.
      auto *subword_encodings = rnn_.Compute(&subword_embeddings_);

      // Select encodings for first subword for each token.
      word_encodings_.resize(length);
      for (int t = 0; t < length; ++t) {
        word_encodings_.set(t, subword_encodings->at(token_start_[t]));
      }

      return &word_encodings_;
    }

   private:
    const SubwordRNNEncoder *encoder_;
    std::vector<int> subword_index_;
    std::vector<int> token_start_;
    Instance subword_;
    Channel subword_embeddings_;
    RNNStackPredictor rnn_;
    Channel word_encodings_;
  };

  Predictor *CreatePredictor() override { return new Predictor(this); }

  // Encoder learner.
  class Learner : public ParserEncoder::Learner {
   public:
    Learner(const SubwordRNNEncoder *encoder)
        : encoder_(encoder),
          subwords_(encoder->subword_),
          gsubword_(encoder->gsubword_),
          subword_embeddings_(encoder->subword_embedding_),
          rnn_(encoder->rnn_),
          word_encodings_(encoder->rnn_output_),
          dsubword_encodings_(encoder->rnn_doutput_) {}

    Channel *Encode(const Document &document, int begin, int end) override {
      // Split tokens into subwords.
      int length = end - begin;
      subword_index_.clear();
      token_start_.resize(length);
      string normalized;
      for (int t = 0; t < length; ++t) {
        token_start_[t] = subword_index_.size();
        const Token &token = document.token(t + begin);
        UTF8::Normalize(token.word(), encoder_->normalization_, &normalized);
        encoder_->subtokenizer_.Tokenize(normalized, &subword_index_);
      }

      // Look up subword embeddings.
      int num_subwords = subword_index_.size();
      subword_embeddings_.resize(num_subwords);
      subwords_.Resize(num_subwords);
      for (int i = 0; i < num_subwords; ++i) {
        Instance &subword = subwords_[i];
        *subword.Get<int>(encoder_->subword_index_) = subword_index_[i];
        subword.Set(encoder_->subword_embedding_, &subword_embeddings_, i);
        subword.Compute();
      }

      // Compute RNN hidden state for each subword token.
      auto *subword_encodings = rnn_.Compute(&subword_embeddings_);

      // Select encodings for first subword for each token.
      word_encodings_.resize(length);
      for (int t = 0; t < length; ++t) {
        word_encodings_.set(t, subword_encodings->at(token_start_[t]));
      }

      return &word_encodings_;
    }

    void Backpropagate(Channel *doutput) override {
      // Create subword gradient.
      int num_subwords = subword_index_.size();
      int num_words = token_start_.size();
      dsubword_encodings_.reset(num_subwords);
      for (int t = 0; t < num_words; ++t) {
        dsubword_encodings_.set(token_start_[t], doutput->at(t));
      }

      // Backpropagate hidden state gradients through RNN.
      Channel *dsubwords = rnn_.Backpropagate(&dsubword_encodings_);

      // Update subword embeddings.
      for (int i = 0; i < num_subwords; ++i) {
        gsubword_.Set(encoder_->gsubword_primal_, &subwords_[i]);
        gsubword_.Set(encoder_->gsubword_dembedding_, dsubwords, i);
        gsubword_.Compute();
      }
    }

    void CollectGradients(Instances *gradients) override {
      gradients->Add(&gsubword_);
      rnn_.CollectGradients(gradients);
    }

   private:
    const SubwordRNNEncoder *encoder_;
    std::vector<int> subword_index_;
    std::vector<int> token_start_;
    InstanceArray subwords_;
    Instance gsubword_;
    Channel subword_embeddings_;
    RNNStackLearner rnn_;
    Channel word_encodings_;
    Channel dsubword_encodings_;
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
  int rnn_type_ = RNN::LSTM;
  int rnn_dim_ = 256;
  int rnn_layers_ = 1;
  bool rnn_bidir_ = true;
  bool rnn_highways_ = false;

  // RNN encoder.
  RNNStack rnn_{"encoder"};
  Tensor *rnn_output_ = nullptr;
  Tensor *rnn_doutput_ = nullptr;
};

REGISTER_PARSER_ENCODER("subrnn", SubwordRNNEncoder);

}  // namespace nlp
}  // namespace sling


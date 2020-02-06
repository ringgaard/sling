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

#include "sling/myelin/rnn.h"
#include "sling/nlp/document/lexical-features.h"
#include "sling/nlp/parser/parser-codec.h"

namespace sling {
namespace nlp {

using namespace sling::myelin;

// Token encoder using lexical features and RNNs.
class LexicalRNNEncoder : public ParserEncoder {
 public:
  // Set up lexical RNN encoder for training.
  void Setup(task::Task *task, Store *commons) override {
    // Set up encoder lexicon.
    string normalization = task->Get("normalization", "d");
    spec_.lexicon.normalization = ParseNormalization(normalization);
    spec_.lexicon.threshold = task->Get("lexicon_threshold", 0);
    spec_.lexicon.max_prefix = task->Get("max_prefix", 0);
    spec_.lexicon.max_suffix = task->Get("max_suffix", 3);
    spec_.feature_padding = 16;

    // Set up word embeddings.
    spec_.word_dim = task->Get("word_dim", 32);
    auto *word_embeddings_input = task->GetInput("word_embeddings");
    if (word_embeddings_input != nullptr) {
      spec_.word_embeddings = word_embeddings_input->resource()->name();
    }
    spec_.train_word_embeddings = task->Get("train_word_embeddings", true);

    // Set up lexical back-off features.
    spec_.prefix_dim = task->Get("prefix_dim", 0);
    spec_.suffix_dim = task->Get("suffix_dim", 16);
    spec_.hyphen_dim = task->Get("hypen_dim", 8);
    spec_.caps_dim = task->Get("caps_dim", 8);
    spec_.punct_dim = task->Get("punct_dim", 8);
    spec_.quote_dim = task->Get("quote_dim", 8);
    spec_.digit_dim = task->Get("digit_dim", 8);

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
    if (words != nullptr) {
      lex_.InitializeLexicon(words, spec_.lexicon);
    }
    auto lexvars = lex_.Build(flow, spec_, learn);
    auto rnnvars = rnn_.Build(flow, lexvars.fv, lexvars.dfv);
    return rnnvars.output;
  }

  // Save encoder to flow.
  void Save(Flow *flow, Builder *spec) override {
    // Save lexicon in flow.
    lex_.SaveLexicon(flow);

    // Save encoder spec.
    spec->Add("type", "lexrnn");
    spec->Add("rnn", rnn_type_);
    spec->Add("dim", rnn_dim_);
    spec->Add("layers", rnn_layers_);
    spec->Add("bidir", rnn_bidir_);
    spec->Add("highways", rnn_highways_);
  }

  // Load encoder from flow.
  void Load(Flow *flow, const Frame &spec) override {
    // Load lexicon from flow.
    lex_.LoadLexicon(flow);

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
    lex_.Initialize(net);
    rnn_.Initialize(net);
  }

  // Encoder predictor.
  class Predictor : public ParserEncoder::Predictor {
   public:
    Predictor(const LexicalRNNEncoder *encoder)
        : features_(encoder->lex_),
          rnn_(encoder->rnn_),
          fv_(encoder->lex_.feature_vector()) {}

    Channel *Encode(const Document &document, int begin, int end) override {
      // Extract features and map through feature embeddings.
      features_.Extract(document, begin, end, &fv_);

      // Compute hidden states for RNN.
      return rnn_.Compute(&fv_);
    }

   private:
    LexicalFeatureExtractor features_;
    RNNStackInstance rnn_;
    Channel fv_;
  };

  Predictor *CreatePredictor() override { return new Predictor(this); }

  // Encoder learner.
  class Learner : public ParserEncoder::Learner {
   public:
    Learner(const LexicalRNNEncoder *encoder)
        : features_(encoder->lex_),
          rnn_(encoder->rnn_) {}

    Channel *Encode(const Document &document, int begin, int end) override {
      // Extract features and map through feature embeddings.
      Channel *fv = features_.Extract(document, begin, end);

      // Compute hidden states for RNN.
      return rnn_.Compute(fv);
    }

    void Backpropagate(myelin::Channel *doutput) override {
      // Backpropagate hidden state gradients through RNN.
      Channel *dfv = rnn_.Backpropagate(doutput);

      // Backpropagate feature vector gradients to feature embeddings.
      features_.Backpropagate(dfv);
    }

    void CollectGradients(Gradients *gradients) override {
      features_.CollectGradients(gradients);
      rnn_.CollectGradients(gradients);
    }

   private:
    LexicalFeatureLearner features_;
    RNNStackLearner rnn_;
  };

  Learner *CreateLearner() override { return new Learner(this); }

 private:
  // Lexical feature specification for encoder.
  LexicalFeatures::Spec spec_;

  // RNN specification.
  int rnn_type_ = myelin::RNN::LSTM;
  int rnn_dim_ = 256;
  int rnn_layers_ = 1;
  bool rnn_bidir_ = true;
  bool rnn_highways_ = false;

  // Lexical feature extractor with embeddings.
  LexicalFeatures lex_{"features"};

  // RNN encoder.
  myelin::RNNStack rnn_{"encoder"};
};

REGISTER_PARSER_ENCODER("lexrnn", LexicalRNNEncoder);

}  // namespace nlp
}  // namespace sling


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

#ifndef SLING_NLP_DOCUMENT_LEXICAL_FEATURES_H_
#define SLING_NLP_DOCUMENT_LEXICAL_FEATURES_H_

#include <string>
#include <unordered_map>

#include "sling/base/types.h"
#include "sling/myelin/flow.h"
#include "sling/myelin/compute.h"
#include "sling/nlp/document/features.h"
#include "sling/nlp/document/lexicon.h"

namespace sling {
namespace nlp {

using namespace sling::myelin;

// Module for token-level lexical feature extraction.
class LexicalFeatures {
 public:
  // Lexicon configuration specification.
  struct LexiconSpec {
    bool normalize_digits = false;  // normalize digits in words
    int threshold = 0;              // threshold frequency for words in lexicon
    int max_prefix = 3;             // max prefix length
    int max_suffix = 3;             // max suffix length
  };

  // Lexical feature specification. Feature is disabled if dimension is zero.
  struct Spec {
    int word_dim = 32;              // word emmedding dimensions
    int prefix_dim = 16;             // prefix embedding dimensions
    int suffix_dim = 16;            // prefix embedding dimensions
    int hyphen_dim = 2;             // hyphenation embedding dimensions
    int caps_dim = 4;               // capitalization embedding dimensions
    int punct_dim = 4;              // punctuation embedding dimensions
    int quote_dim = 2;              // quote feature embedding dimensions
    int digit_dim = 4;              // digit feature embedding dimensions
  };

  LexicalFeatures(const string &name = "features") : name_(name) {}

  // Load lexicon from existing model.
  void LoadLexicon(Flow *flow);

  // Initialize lexicon from dictionary.
  void InitializeLexicon(Vocabulary::Iterator *words, const LexiconSpec &spec);

  // Build flow for lexical feature extraction. The lexicon must be initialized
  // before building the flow.
  Flow::Variable *Build(const Library &library, const Spec &spec, Flow *flow,
                        bool learn);

  // Initialize feature extractor from existing model.
  void Initialize(const Network &net);

  // Load pre-trained word embeddings. Returns the number of words which were
  // initialized from the pre-trained embeddings.
  int LoadWordEmbeddings(const string &filename);

  // Lexicon.
  const Lexicon &lexicon() const { return lexicon_; }

  // Feature vector output.
  Tensor *feature_vector() const { return feature_vector_; }

 private:
  string name_;                        // cell name
  Lexicon lexicon_;                    // lexicon for word vocabulary

  Cell *features_ = nullptr;           // feature extractor cell
  Tensor *word_feature_ = nullptr;     // word feature
  Tensor *prefix_feature_ = nullptr;   // prefix feature
  Tensor *suffix_feature_ = nullptr;   // suffix feature
  Tensor *hyphen_feature_ = nullptr;   // hyphenation feature
  Tensor *caps_feature_ = nullptr;     // capitalization feature
  Tensor *punct_feature_ = nullptr;    // punctuation feature
  Tensor *quote_feature_ = nullptr;    // quote feature
  Tensor *digit_feature_ = nullptr;    // digit feature
  Tensor *feature_vector_ = nullptr;   // output feature vector
  Tensor *word_embeddings_ = nullptr;  // word embedding matrix

  int prefix_size_ = 0;                // max prefix length
  int suffix_size_ = 0;                // max suffix length

  Cell *gfeatures_ = nullptr;          // gradient cell
  Tensor *d_feature_vector_;           // feature vector input to gradient
  Tensor *primal_;                     // reference to primal cell

  friend class LexicalFeatureExtractor;
  friend class LexicalFeatureLearner;
};

// Lexical feature extractor for extracting features from document tokens and
// mapping these though feature embeddings.
class LexicalFeatureExtractor {
 public:
  LexicalFeatureExtractor(const LexicalFeatures &lex)
      : lex_(lex), data_(lex.features_) {}

  // Compute feature vector for token.
  void Compute(const DocumentFeatures &features, int index, float *fv);

  // Extract lexical features from a range of tokens in a document and output
  // the feature vectors to a channel.
  void Extract(const Document &document, int begin, int end, Channel *fv);

  // Data instance for feature extraction.
  Instance *data() { return &data_; }

 private:
  const LexicalFeatures &lex_;
  Instance data_;
};

// Lexical feature learner for training feature embeddings.
class LexicalFeatureLearner {
 public:
  LexicalFeatureLearner(const LexicalFeatures &lex)
      : lex_(lex), fv_(lex.feature_vector_), gradient_(lex.gfeatures_) {}
  ~LexicalFeatureLearner() { for (auto *e : extractors_) delete e; }

  // Extract features and compute feature vectors for all tokens in range.
  // Return channel with feature vectors for each token.
  Channel *Extract(const Document &document, int begin, int end);

  // Backpropagate feature vector gradients to feature embeddings.
  void Backpropagate(Channel *dfv);

  // Accumulated gradients.
  Instance *gradient() { return &gradient_; }

  // Clear gradients.
  void Clear() { gradient_.Clear(); }

 private:
  const LexicalFeatures &lex_;
  std::vector<LexicalFeatureExtractor *> extractors_;
  Channel fv_;
  Instance gradient_;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_DOCUMENT_LEXICAL_FEATURES_H_


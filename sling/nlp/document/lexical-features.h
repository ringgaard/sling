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
  // Lexical feature specification. Feature is disabled if dimension is zero.
  struct Spec {
    int word_dim = 64;           // word emmedding dimensions
    int max_prefix = 3;          // max prefix length
    int max_suffix = 3;          // max suffix length
    int prefix_dim = 16;         // prefix embedding dimensions
    int suffix_dim = 16;         // prefix embedding dimensions
    bool hyphen_dim = 8;         // hyphenation embedding dimensions
    bool caps_dim = 8;           // capitalization embedding dimensions
    bool punct_dim = 8;          // punctuation embedding dimensions
    bool quote_dim = 8;          // quote feature embedding dimensions
    bool digit_dim = 8;          // digit feature embedding dimensions
  };

  // Dictionary type mapping words to frequency.
  typedef std::unordered_map<string, int> Dictionary;

  LexicalFeatures(const string &name = "features") : name_(name) {}

  // Load lexicon from existing model.
  void LoadLexicon(Flow *flow);

  // Initialize lexicon from dictionary.
  void InitializeLexicon(const Dictionary &dictionary,
                         bool normalize_digits = false,
                         int threshold = 0);

  // Build flow for lexical feature extraction. The lexicon must be initialized
  // before building the flow.
  void Build(const Spec &spec, Flow *flow, bool learning);

  // Initialize feature extractor from existing model.
  void InitializeModel(const Network &net);

  // Load pre-trained word embeddings.
  void LoadWordEmbeddings(const string &filename);

  // Lexicon.
  const Lexicon &lexicon() const { return lexicon_; }

  // Size of output feature vector.
  int feature_vector_dims() const { return feature_vector_dims_; }

 private:
  string name_;                       // cell name
  Lexicon lexicon_;                   // lexicon for word vocabulary

  Cell *features_ = nullptr;          // feature extractor cell
  Tensor *word_feature_ = nullptr;    // word feature
  Tensor *prefix_feature_ = nullptr;  // prefix feature
  Tensor *suffix_feature_ = nullptr;  // suffix feature
  Tensor *hyphen_feature_ = nullptr;  // hyphenation feature
  Tensor *caps_feature_ = nullptr;    // capitalization feature
  Tensor *punct_feature_ = nullptr;   // punctuation feature
  Tensor *quote_feature_ = nullptr;   // quote feature
  Tensor *digit_feature_ = nullptr;   // digit feature
  Tensor *feature_vector_ = nullptr;  // output feature vector

  Connector *fv_cnx_;

  int prefix_size_ = 0;               // max prefix length
  int suffix_size_ = 0;               // max suffix length
  int feature_vector_dims_ = 0;       // size of output feature vector

  Cell *gfeatures_ = nullptr;         // gradient cell
  Tensor *d_feature_vector_;          // feature vector input to gradient
  Tensor *primal_;                    // reference to primal cell

  friend class LexicalFeatureExtractor;
  friend class LexicalFeatureLearner;
};

// Lexical feature extractor for extracting features from document tokens and
// mapping these though feature embeddings.
class LexicalFeatureExtractor {
 public:
  LexicalFeatureExtractor(const LexicalFeatures &features)
      : features_(features), data_(features.features_) {}

  // Compute feature vector from token in document.
  void Compute(const DocumentFeatures &document, int token, float *fv);

  // Extract lexical features from a range of document in a socument and output
  // the feature vectors to a channel.
  void Extract(const DocumentFeatures &document, int begin, int end,
               Channel *fv);

  // Data instance for feature extraction.
  Instance *data() { return &data_; }

 private:
  const LexicalFeatures &features_;
  Instance data_;
};

// Lexical feature learner for training feature embeddings.
class LexicalFeatureLearner {
 public:
  LexicalFeatureLearner(const LexicalFeatures &features)
      : features_(features),
        fv_(features.fv_cnx_),
        gradient_(features.gfeatures_) {}
  ~LexicalFeatureLearner() { for (auto *e : extractors_) delete e; }

  // Extract features and compute feature vectors for all tokens in range.
  // Return channel with feature vectors for each token.
  Channel *Extract(const DocumentFeatures &document, int begin, int end);

  // Backpropagate feature vector gradients to feature embeddings.
  void Backpropagate(Channel *dfv);

  // Accumulated gradients.
  Instance *gradient() { return &gradient_; }

  // Clear gradients.
  void clear() { gradient_.Clear(); }

 private:
  const LexicalFeatures &features_;
  std::vector<LexicalFeatureExtractor *> extractors_;
  Channel fv_;
  Instance gradient_;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_DOCUMENT_LEXICAL_FEATURES_H_


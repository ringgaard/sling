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

#include "sling/nlp/document/lexical-features.h"

namespace sling {
namespace nlp {

void LexicalFeatures::LoadLexicon(Flow *flow) {
  // Load word vocabulary.
  Flow::Blob *vocabulary = flow->DataBlock("lexicon");
  CHECK(vocabulary != nullptr);
  lexicon_.InitWords(vocabulary->data, vocabulary->size);
  bool normalize = vocabulary->attrs.Get("normalize_digits", false);
  int oov = vocabulary->attrs.Get("oov", -1);
  lexicon_.set_normalize_digits(normalize);
  lexicon_.set_oov(oov);

  // Load affix tables.
  Flow::Blob *prefix_table = flow->DataBlock("prefixes");
  if (prefix_table != nullptr) {
    lexicon_.InitPrefixes(prefix_table->data, prefix_table->size);
  }
  Flow::Blob *suffix_table = flow->DataBlock("suffixes");
  if (suffix_table != nullptr) {
    lexicon_.InitSuffixes(suffix_table->data, suffix_table->size);
  }
}

void LexicalFeatures::InitializeLexicon(const Dictionary &dictionary,
                                        bool normalize_digits,
                                        int threshold) {
  // TODO: implement.
}


void LexicalFeatures::Build(const Spec &spec, Flow *flow, bool learning) {
  // TODO: implement
}

void LexicalFeatures::InitializeModel(const Network &net) {
  // Get tensors.
  features_ = net.GetCell(name_);
  word_feature_ = net.LookupParameter(name_ + "/word");
  prefix_feature_ = net.LookupParameter(name_ + "/prefix");
  suffix_feature_ = net.LookupParameter(name_ + "/suffix");
  hyphen_feature_ = net.LookupParameter(name_ + "/hyphen");
  caps_feature_ = net.LookupParameter(name_ + "/caps");
  punct_feature_ = net.LookupParameter(name_ + "/punct");
  quote_feature_ = net.LookupParameter(name_ + "/quote");
  digit_feature_ = net.LookupParameter(name_ + "/digit");
  feature_vector_ = net.GetParameter(name_ + "/feature_vector");

  // Optionally initialize gradient cell.
  gfeatures_ = net.LookupCell("g" + name_);
  if (gfeatures_ != nullptr) {
    const string &gname = gfeatures_->name();
    d_feature_vector_ = net.GetParameter(gname + "/d_feature_vector");
    primal_ = net.GetParameter(gname + "/primal");
  }

  // Get feature sizes.
  if (prefix_feature_ != nullptr) prefix_size_ = prefix_feature_->elements();
  if (suffix_feature_ != nullptr) suffix_size_ = suffix_feature_->elements();
  feature_vector_dims_ = feature_vector_->elements();
}

void LexicalFeatures::LoadWordEmbeddings(const string &filename) {
  // TODO: implement
};

void LexicalFeatureExtractor::Compute(const DocumentFeatures &document,
                                      int token, float *fv) {
  // Extract word feature.
  if (features_.word_feature_) {
    *data_.Get<int>(features_.word_feature_) = document.word(token);
  }

  // Extract prefix feature.
  if (features_.prefix_feature_) {
    Affix *affix = document.prefix(token);
    int *a = data_.Get<int>(features_.prefix_feature_);
    for (int n = 0; n < features_.prefix_size_; ++n) {
      if (affix != nullptr) {
        *a++ = affix->id();
        affix = affix->shorter();
      } else {
        *a++ = -1;
      }
    }
  }

  // Extract suffix feature.
  if (features_.suffix_feature_) {
    Affix *affix = document.suffix(token);
    int *a = data_.Get<int>(features_.suffix_feature_);
    for (int n = 0; n < features_.suffix_size_; ++n) {
      if (affix != nullptr) {
        *a++ = affix->id();
        affix = affix->shorter();
      } else {
        *a++ = -1;
      }
    }
  }

  // Extract hyphen feature.
  if (features_.hyphen_feature_) {
    *data_.Get<int>(features_.hyphen_feature_) = document.hyphen(token);
  }

  // Extract capitalization feature.
  if (features_.caps_feature_) {
    *data_.Get<int>(features_.caps_feature_) = document.capitalization(token);
  }

  // Extract punctuation feature.
  if (features_.punct_feature_) {
    *data_.Get<int>(features_.punct_feature_) = document.punctuation(token);
  }

  // Extract quote feature.
  if (features_.quote_feature_) {
    *data_.Get<int>(features_.quote_feature_) = document.quote(token);
  }

  // Extract digit feature.
  if (features_.digit_feature_) {
    *data_.Get<int>(features_.digit_feature_) = document.digit(token);
  }

  // Set reference to output feature vector.
  data_.SetReference(features_.feature_vector_, fv);

  // Map features through embeddings.
  data_.Compute();
}

void LexicalFeatureExtractor::Extract(const DocumentFeatures &document,
                                      int begin, int end, Channel *fv) {
  int length = end - begin;
  fv->resize(length);
  for (int token = begin; token < end; ++token) {
    float *f = reinterpret_cast<float *>(fv->at(token - begin));
    Compute(document, token, f);
  }
}

Channel *LexicalFeatureLearner::Extract(const DocumentFeatures &document,
                                        int begin, int end) {
  // Extract features and compute feature vector for all tokens in range.
  for (auto *e : extractors_) delete e;
  extractors_.clear();
  int length = end - begin;
  fv_.resize(length);
  for (int token = begin; token < end; ++token) {
    auto *e = new LexicalFeatureExtractor(features_);
    extractors_.push_back(e);
    float *f = reinterpret_cast<float *>(fv_.at(token - begin));
    e->Compute(document, token, f);
  }
  return &fv_;
}

void LexicalFeatureLearner::Backpropagate(Channel *dfv) {
  CHECK_EQ(dfv->size(), fv_.size());
  for (int i = 0; i < fv_.size(); ++i) {
    gradient_.Set(features_.d_feature_vector_, dfv, i);
    gradient_.Set(features_.primal_, extractors_[i]->data());
    gradient_.Compute();
  }
}

}  // namespace nlp
}  // namespace sling


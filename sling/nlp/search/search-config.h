// Copyright 2022 Ringgaard Research ApS
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

#ifndef SLING_NLP_SEARCH_CONFIG_H_
#define SLING_NLP_SEARCH_CONFIG_H_

#include <string>
#include <unordered_set>

#include "sling/base/types.h"
#include "sling/frame/object.h"
#include "sling/frame/store.h"
#include "sling/nlp/document/phrase-tokenizer.h"
#include "sling/nlp/wiki/wiki.h"
#include "sling/util/unicode.h"

namespace sling {
namespace nlp {

// Search engine configuration.
class SearchConfiguration {
 public:
  // Load search index configuration.
  void Load(Store *store, const string &filename, bool dictionary = false);

  // Include aliases in dictionary.
  bool dictionary_aliases() const { return dictionary_aliases_; }

  // Languages for search terms.
  const HandleSet &languages() const { return languages_; }

  // Term normalization.
  const string &normalization() const { return normalization_; }

  // Term normalization flags.
  Normalization norm() const {
    return ParseNormalization(normalization_);
  }

  // Phrase tokenizer.
  const PhraseTokenizer &tokenizer() const { return tokenizer_; }

  // Wiki item types.
  const WikimediaTypes &wikitypes() const { return wikitypes_; }

  // Check if item type is skipped in indexing.
  bool skipped(Handle type) const {
    return wikitypes_.IsNonEntity(type) || wikitypes_.IsBiographic(type);
  }

  // Check if properties for item are omitted from indexing.
  bool omit(const string &itemid) const {
    return omitted_.count(itemid) > 0;
  }

  // Check if language is skipped in indexing.
  bool foreign(Handle lang) const {
    return !lang.IsNil() && languages_.count(lang) == 0;
  }

  // Number of buckets in search term map.
  int buckets() const { return buckets_; }

  // Check if property is indexed.
  Handle index(Handle property) const {
    auto f = properties_.find(property);
    return f != properties_.end() ? f->second : Handle::nil();
  }

  // Return stop word fingerprints.
  const std::unordered_set<uint64> &stopwords() const { return stopwords_; }

  // Check if term is a stopword.
  bool stopword(uint64 term) const {
    return term == 1 || stopwords_.count(term) > 0;
  }

  // Compute term fingerprint.
  uint64 fingerprint(Text word) {
    return tokenizer_.TokenFingerprint(word);
  }

 private:
  // Include aliases in dictionary.
  bool dictionary_aliases_ = false;

  // Languages for search terms.
  HandleSet languages_;

  // Indexed properties.
  HandleMap<Handle> properties_;

  // Stopwords.
  std::unordered_set<uint64> stopwords_;

  // Items where properties are omitted from indexing.
  std::unordered_set<string> omitted_;

  // Term normalization.
  string normalization_;

  // Wiki item types.
  WikimediaTypes wikitypes_;

  // Phrase tokenizer for computing term fingerprints.
  PhraseTokenizer tokenizer_;

  // Number of buckets in search term map.
  int buckets_ = 1 << 20;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_SEARCH_CONFIG_H_


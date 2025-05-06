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

#ifndef SLING_NLP_SEARCH_ENGINE_H_
#define SLING_NLP_SEARCH_ENGINE_H_

#include "sling/base/types.h"
#include "sling/nlp/document/phrase-tokenizer.h"
#include "sling/nlp/search/search-index.h"
#include "sling/util/top.h"

namespace sling {
namespace nlp {

class SearchEngine {
 public:
  // Query hit.
  typedef SearchIndex::Document Document;
  struct Hit {
    Hit(const Document *document) : document(document), score(0) {}
    Text id() const { return document->id(); }
    const Document *document;
    int score;
  };

  // Search hit comparison operator.
  struct HitCompare {
    bool operator()(const Hit &a, const Hit &b) {
      return a.score > b.score;
    }
  };

  // Search results for selecting top-k results.
  typedef Top<Hit, HitCompare> Hits;

  // Search results.
  class Results {
   public:
    Results(int limit) : hits_(limit) {}

    // Return search matches.
    const Hits &hits() const { return hits_; }

    // Score document against query.
    int Score(const Document *document);

   private:
    // Check for unigram query match.
    bool Unigram(uint16 term) const;

    // Check for bigram query match.
    bool Bigram(uint16 term1, uint16 term2) const;

    // Work fingerprints for search terms.
    std::vector<uint16> query_terms_;

    // Search hits.
    Hits hits_;

    // Total number of matches.
    int total_hits_ = 0;

    friend class SearchEngine;
  };

  // Load search engine index.
  void Load(const string &filename);

  // Search for matches in search index and put the k-best matches into the
  // result list. Returns the total number of matches.
  int Search(Text query, Results *results);

  // Check if search index has been loaded.
  bool loaded() const { return index_.loaded(); }

  // Tokenize text.
  void tokenize(Text text, std::vector<uint64> *tokens) const {
    tokenizer_.TokenFingerprints(text, tokens);
  }

 private:
  // Search index.
  SearchIndex index_;

  // Tokenizer for tokenizing query.
  PhraseTokenizer tokenizer_;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_SEARCH_ENGINE_H_

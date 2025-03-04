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

#include <algorithm>
#include <string>
#include <vector>

#include "sling/nlp/search/search-engine.h"

#include "sling/util/unicode.h"

namespace sling {
namespace nlp {

void SearchEngine::Load(const string &filename) {
  // Load search index.
  index_.Load(filename);

  // Initialize tokenizer.
  tokenizer_.set_normalization(ParseNormalization(index_.normalization()));
}

int SearchEngine::Search(Text query, Results *results) {
  // Return empty result if index has not been loaded.
  results->Reset(this);
  if (!loaded()) return 0;

  // Tokenize query.
  tokenize(query, &results->query_terms_);

  // Look up posting lists for tokens in search index.
  std::vector<const SearchIndex::Term *> terms;
  for (uint64 token : results->query_terms_) {
    if (index_.stopword(token)) continue;

    const SearchIndex::Term *term = index_.Find(token);
    if (term == nullptr) return 0;

    terms.push_back(term);
  }
  if (terms.empty()) return 0;

  // Sort search terms by frequency starting with the most rare terms.
  std::sort(terms.begin(), terms.end(),
    [](const SearchIndex::Term *a, const SearchIndex::Term *b) {
        return a->num_documents() < b->num_documents();
    });

  // Initialize candidates from first term.
  const uint32 *candidates_begin = terms[0]->documents();
  const uint32 *candidates_end = candidates_begin + terms[0]->num_documents();

  // The matches[0] array contains the current matches and matches[1] receives
  // the new matches. These are swapped at the end of each iteration.
  std::vector<uint32> matches[2];

  // Match the rest of the search terms.
  for (int i = 1; i < terms.size(); ++i) {
    const SearchIndex::Term *term = terms[i];
    const uint32 *c = candidates_begin;
    const uint32 *cend = candidates_end;
    const uint32 *e = term->documents();
    const uint32 *eend = e + term->num_documents();

    // Intersect current candidates with postings for term.
    std::vector<uint32> &results = matches[1];
    results.clear();
    while (c < cend && e < eend) {
      if (*e < *c) {
        e++;
      } else if (*c < *e) {
        c++;
      } else {
        results.push_back(*c);
        c++;
        e++;
      }
    }

    // Bail out if there are no more candidates.
    if (results.empty()) return 0;

    // Swap match arrays.
    matches[0].swap(matches[1]);
    candidates_begin = matches[0].data();
    candidates_end = candidates_begin + matches[0].size();
  }

  // Output results.
  int hits = candidates_end - candidates_begin;
  results->total_hits_ = hits;
  for (const uint32 *c = candidates_begin; c != candidates_end; ++c) {
    const Document *document = index_.GetDocument(*c);
    results->hits_.push(document);
  }
  results->hits_.sort();
  return hits;
}

void SearchEngine::Results::Reset(const SearchEngine *search) {
  search_ = search;
  query_terms_.clear();
  hits_.clear();
}

int SearchEngine::Results::Score(Text text, int base) const {
  std::vector<uint64> tokens;
  search_->tokenize(text, &tokens);
  int unigrams = 0;
  int bigrams = 0;
  uint64 prev = 0;
  for (uint64 token : tokens) {
    if (Unigram(token)) {
      unigrams++;
      if (prev != 0 && Bigram(prev, token)) bigrams++;
    }
    prev = token;
  }
  int boost = 100 * bigrams + 10 * unigrams + 1;
  if (unigrams == tokens.size()) boost++;
  return (base + 1) * boost;
}

bool SearchEngine::Results::Unigram(uint64 term) const {
  for (int i = 0; i < query_terms_.size(); ++i) {
    if (query_terms_[i] == term) return true;
  }
  return false;
}

bool SearchEngine::Results::Bigram(uint64 term1, uint64 term2) const {
  for (int i = 0; i < query_terms_.size() - 1; ++i) {
    if (query_terms_[i] == term1 && query_terms_[i + 1] == term2) return true;
  }
  return false;
}

}  // namespace nlp
}  // namespace sling

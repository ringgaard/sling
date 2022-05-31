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

void SearchEngine::Search(Text query, Results *results) {
  // Tokenize query.
  std::vector<uint64> tokens;
  tokenizer_.TokenFingerprints(query, &tokens);

  // Look up posting lists for tokens in search index.
  results->clear();
  std::vector<const SearchIndex::Term *> terms;
  for (uint64 token : tokens) {
    if (token != 1) {
      const SearchIndex::Term *term = index_.Find(token);
      terms.push_back(term);
    }
  }
  if (terms.empty()) return;

  // Sort search terms by frequency starting with the most rare terms.
  std::sort(terms.begin(), terms.end(),
    [](const SearchIndex::Term *a, const SearchIndex::Term *b) {
        return a->num_entities() < b->num_entities();
    });

  // Initialize candidates from first term.
  const uint32 *candidates_begin = terms[0]->entities();
  const uint32 *candidates_end = candidates_begin + terms[0]->num_entities();

  // The matches[0] contains the current matches and matches[1] received the
  // new matches. These are swapped at the end of each iteration.
  std::vector<uint32> matches[2];

  // Match the rest of the search terms.
  for (int i = 1; i < terms.size(); ++i) {
    const SearchIndex::Term *term = terms[i];
    const uint32 *c = candidates_begin;
    const uint32 *cend = candidates_end;
    const uint32 *e = term->entities();
    const uint32 *eend = e + term->num_entities();

    // Intersect current candidates with postings for term.
    std::vector<uint32> &results = matches[1];
    results.clear();
    while (c < cend && e < eend) {
      if (*e > *c) {
        e++;
      } else if (*c < *e) {
        c++;
      } else {
        results.push_back(*c);
        c++;
        e++;
      }
    }

    // Swap match arrays.
    matches[0].swap(matches[1]);
    candidates_begin = matches[0].data();
    candidates_end = candidates_begin + matches[0].size();
  }

  // Output results.
  for (const uint32 *c = candidates_begin; c != candidates_begin; ++c) {
    const Entity *entity = index_.GetEntity(*c);
    results->push(entity);
  }
  results->sort();
}

}  // namespace nlp
}  // namespace sling

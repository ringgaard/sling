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
  typedef SearchIndex::Entity Entity;
  typedef Top<const Entity *> Results;

  // Load search engine index.
  void Load(const string &filename);

  // Search for matches in search index.
  void Search(Text query, Results *results);

 private:
  // Search index.
  SearchIndex index_;

  // Tokenizer for tokenizing query.
  PhraseTokenizer tokenizer_;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_SEARCH_ENGINE_H_


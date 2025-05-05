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

#include "sling/nlp/search/search-index.h"

namespace sling {
namespace nlp {

void SearchIndex::Load(const string &filename) {
  // Load search index repository. Do not preload posting lists and documents.
  repository_.Open(filename);
  repository_.LoadBlock("TermItems", false);
  repository_.LoadBlock("DocumentItems", false);
  repository_.LoadAll();
  repository_.Close();

  // Get search index parameters.
  JSON json = JSON::Read(repository_.GetBlockString("params"));
  params_.MoveFrom(json);

  // Initialize document table.
  document_index_.Initialize(repository_);

  // Initialize term index.
  term_index_.Initialize(repository_);
  num_buckets_ = term_index_.num_buckets();

  // Initialize stopwords.
  const uint64 *stopwords;
  repository_.FetchBlock("stopwords", &stopwords);
  int num_stopwords = repository_.GetBlockSize("stopwords") / sizeof(uint64);
  for (int i = 0; i < num_stopwords; ++i) {
    stopwords_.insert(stopwords[i]);
  }
}

const SearchIndex::Term *SearchIndex::Find(uint64 fp) const {
  int bucket = fp % num_buckets_;
  const Term *term = term_index_.GetBucket(bucket);
  const Term *end = term_index_.GetBucket(bucket + 1);
  while (term < end) {
    if (term->fingerprint() == fp) return term;
    term = term->next();
  }

  return nullptr;
}

}  // namespace nlp
}  // namespace sling

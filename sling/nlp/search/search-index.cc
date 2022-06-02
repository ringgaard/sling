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
  // Load search index repository.
  repository_.Read(filename);

  // Initialize entity table.
  entity_index_.Initialize(repository_);

  // Initialize term index.
  term_index_.Initialize(repository_);
  num_buckets_ = term_index_.num_buckets();
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

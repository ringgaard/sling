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

#include "sling/nlp/search/search-dictionary.h"

#include "sling/util/fingerprint.h"

namespace sling {
namespace nlp {

void SearchDictionary::Load(const string &filename) {
  // Load search dictionary repository.
  repository_.Read(filename);

  // Initialize search dictionary index.
  index_.Initialize(repository_);
}

const SearchDictionary::Item *SearchDictionary::Find(Text itemid) const {
  uint64 fp = Fingerprint(itemid.data(), itemid.size());
  int bucket = fp % index_.num_buckets();
  const Item *item = index_.GetBucket(bucket);
  const Item *end = index_.GetBucket(bucket + 1);
  while (item < end) {
    if (itemid == item->id()) return item;
    item = item->next();
  }
  return nullptr;
}

}  // namespace nlp
}  // namespace sling

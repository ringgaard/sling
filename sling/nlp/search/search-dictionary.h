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

#ifndef SLING_NLP_SEARCH_DICTIONARY_H_
#define SLING_NLP_SEARCH_DICTIONARY_H_

#include <string>

#include "sling/base/types.h"
#include "sling/file/repository.h"
#include "sling/string/text.h"

namespace sling {
namespace nlp {

// Search dictionary with name terms for each item.
class SearchDictionary {
 public:
  // Search terms for item.
  class Item : public RepositoryObject {
   public:
    // Item id.
    Text id() const { return Text(id_ptr(), *idlen_ptr()); }

    // Terms array.
    const uint64 *terms() const { return term_ptr(); }

    // Length of term list.
    int num_terms() const { return *termlen_ptr(); }

    // Return next item in list.
    const Item *next() const {
      int size = sizeof(uint8) + *idlen_ptr() +
                 sizeof(uint32) + num_terms() * sizeof(uint64);
      const char *self = reinterpret_cast<const char *>(this);
      return reinterpret_cast<const Item *>(self + size);
    }

   private:
    // Entity id.
    REPOSITORY_FIELD(uint8, idlen, 1, 0);
    REPOSITORY_FIELD(char, id, *idlen_ptr(), AFTER(idlen));

    // Term list.
    REPOSITORY_FIELD(uint32, termlen, 1, AFTER(id));
    REPOSITORY_FIELD(uint64, term, num_terms(), AFTER(termlen));
  };

  // Load search dictionary from file.
  void Load(const string &filename);

  // Find item in dictionary. Return null if item is not found.
  const Item *Find(Text itemid) const;

 private:
  // Search dictionary index in repository.
  class Index : public RepositoryMap<Item> {
   public:
    // Initialize phrase index.
    void Initialize(const Repository &repository) {
      Init(repository, "SearchDictionary");
    }

    // Return first element in bucket.
    const Item *GetBucket(int bucket) const { return GetObject(bucket); }
  };

  // Repository with search dictionary.
  Repository repository_;

  // Item index.
  Index index_;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_SEARCH_DICTIONARY_H_


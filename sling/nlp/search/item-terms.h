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

#ifndef SLING_NLP_SEARCH_ITEM_TERMS_H_
#define SLING_NLP_SEARCH_ITEM_TERMS_H_

#include "sling/base/types.h"
#include "sling/file/repository.h"
#include "sling/string/text.h"

namespace sling {
namespace nlp {

// Item with name term fingerprints.
class ItemTerms : public RepositoryObject {
 public:
  // Item id.
  Text itemid() const { return Text(id_ptr(), *idlen_ptr()); }

  // Length of term list.
  int num_terms() const { return *termlen_ptr(); }

 private:
  // Entity id.
  REPOSITORY_FIELD(uint8, idlen, 1, 0);
  REPOSITORY_FIELD(char, id, *idlen_ptr(), AFTER(idlen));

  // Term list.
  REPOSITORY_FIELD(uint32, termlen, 1, AFTER(id));
  REPOSITORY_FIELD(uint64, term, num_terms(), AFTER(termlen));
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_SEARCH_ITEM_TERMS_H_


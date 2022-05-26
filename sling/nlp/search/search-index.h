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

#ifndef SLING_NLP_SEARCH_INDEX_H_
#define SLING_NLP_SEARCH_INDEX_H_

#include <string>

#include "sling/base/types.h"
#include "sling/file/repository.h"

namespace sling {
namespace nlp {

// Search index with item posting lists for each search term.
class SearchIndex {
 public:
  // Load search index from file.
  void Load(const string &filename);

 private:
  // Repository with search index.
  Repository repository_;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_SEARCH_INDEX_H_


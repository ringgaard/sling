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

#include <vector>

#include "sling/nlp/search/search-config.h"

#include "sling/frame/serialization.h"

namespace sling {
namespace nlp {

void SearchConfiguration::Load(Store *store,
                               const string &filename,
                               bool dictionary) {
  FileReader reader(store, filename);
  Frame config = reader.Read().AsFrame();
  CHECK(config.valid());
  dictionary_aliases_ = config.GetBool("dictionary_aliases");

  // Get languages for indexing.
  const char *lang = dictionary ? "dictionary_languages" : "index_languages";
  Array langs = config.Get(lang).AsArray();
  CHECK(langs.valid());
  for (int i = 0; i < langs.length(); ++i) {
    languages_.add(langs.get(i));
  }

  // Get indexed properties.
  Frame indexed = config.GetFrame("indexed");
  CHECK(indexed.valid());
  for (const Slot &s : indexed) {
    properties_[s.name] = s.value;
  }

  // Get omitted items.
  Array omitted = config.Get("omitted").AsArray();
  if (omitted.valid()) {
    for (int i = 0; i < omitted.length(); ++i) {
      String itemid(store, omitted.get(i));
      omitted_.insert(itemid.value());
    }
  }

  // Set up phrase normalization.
  normalization_ = config.GetString("normalization");
  tokenizer_.set_normalization(norm());

  // Get stopwords.
  Array stopwords = config.Get("stopwords").AsArray();
  std::vector<uint64> tokens;
  CHECK(stopwords.valid());
  for (int i = 0; i < stopwords.length(); ++i) {
    Text stopword = String(store, stopwords.get(i)).text();
    tokenizer_.TokenFingerprints(stopword, &tokens);
    for (uint64 fp : tokens) stopwords_.insert(fp);
  }

  // Get number of term buckets.
  buckets_ = config.GetInt("buckets", buckets_);

  // Initialize wiki types.
  wikitypes_.Init(store);
}

}  // namespace nlp
}  // namespace sling

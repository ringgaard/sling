// Copyright 2017 Google Inc.
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

#include <utility>

#include "sling/file/textmap.h"
#include "sling/frame/object.h"
#include "sling/frame/store.h"
#include "sling/frame/serialization.h"
#include "sling/nlp/kb/facts.h"
#include "sling/task/process.h"
#include "sling/util/bloom.h"
#include "sling/util/sortmap.h"

namespace sling {
namespace nlp {

using namespace task;

// Extract fact and category lexicons from items.
class FactLexiconExtractor : public Process {
 public:
  void Run(Task *task) override {
    // Get parameters.
    int64 bloom_size = task->Get("bloom_size", 4000000000L);
    int bloom_hashes = task->Get("bloom_hashes", 4);
    int fact_threshold = task->Get("fact_threshold", 10);
    int category_threshold = task->Get("category_threshold", 10);

    // Set up counters.
    Counter *num_items = task->GetCounter("items");
    Counter *num_facts = task->GetCounter("facts");
    Counter *num_fact_types = task->GetCounter("fact_types");
    Counter *num_filtered = task->GetCounter("filtered_facts");
    Counter *num_facts_selected = task->GetCounter("facts_selected");
    Counter *num_categories_selected = task->GetCounter("categories_selected");

    // Load knowledge base.
    Store commons;
    LoadStore(task->GetInputFile("kb"), &commons);

    // Resolve symbols.
    Names names;
    Name p_item_category(names, "/w/item/category");
    Name n_item(names, "/w/item");
    Name p_instance_of(names, "P31");
    Name n_wikimedia_category(names, "Q4167836");
    names.Bind(&commons);

    // Initialize fact catalog.
    FactCatalog catalog;
    catalog.Init(&commons);
    commons.Freeze();

    // A Bloom filter is used for checking for singleton facts. It is used as
    // a fast and compact check for detecting if a fact is a new fact. The
    // probabilistic nature of the Bloom filter means that the fact instance
    // counts can be off by one.
    BloomFilter filter(bloom_size, bloom_hashes);

    // The categories are collected in a sortable hash map so the most frequent
    // categories can be selected.
    SortableMap<Handle, int64, HandleHash> category_lexicon;

    // The facts are collected in a sortable hash map where the key is the
    // fact fingerprint. The name of the fact is stored in a nul-terminated
    // dynaminally allocated string.
    SortableMap<int64, std::pair<int64, char *>> fact_lexicon;

    // Extract facts from all items in the knowledge base.
    commons.ForAll([&](Handle handle) {
      Frame item(&commons, handle);
      if (!item.IsA(n_item)) return;

      // Skip categories.
      if (item.GetHandle(p_instance_of) == n_wikimedia_category) return;

      // Extract facts from item.
      Store store(&commons);
      Facts facts(&catalog, &store);
      facts.Extract(handle);

      // Add facts to fact lexicon.
      for (Handle fact : facts.list()) {
        int64 fp = store.Fingerprint(fact);
        if (filter.add(fp)) {
          auto &entry = fact_lexicon[fp];
          if (entry.second == nullptr) {
            entry.second = strdup(ToText(&store, fact).c_str());
            num_fact_types->Increment();
          }
          entry.first++;
        } else {
          num_filtered->Increment();
        }
      }
      num_facts->Increment(facts.list().size());

      // Extract categories from item.
      for (const Slot &s : item) {
        if (s.name == p_item_category) {
          category_lexicon[s.value]++;
        }
      }

      num_items->Increment();
    });
    task->GetCounter("num_categories")->Increment(category_lexicon.map.size());

    // Write fact lexicon to text map.
    fact_lexicon.sort();
    TextMapOutput factout(task->GetOutputFile("factmap"));
    for (int i = fact_lexicon.array.size() - 1; i >= 0; --i) {
      Text fact(fact_lexicon.array[i]->second.second);
      int64 count = fact_lexicon.array[i]->second.first;
      if (count < fact_threshold) break;
      factout.Write(fact, count);
      num_facts_selected->Increment();
    }
    factout.Close();

    // Write category lexicon to text map.
    category_lexicon.sort();
    TextMapOutput catout(task->GetOutputFile("catmap"));
    for (int i = category_lexicon.array.size() - 1; i >= 0; --i) {
      Frame cat(&commons, category_lexicon.array[i]->first);
      int64 count = category_lexicon.array[i]->second;
      if (count < category_threshold) break;
      catout.Write(cat.Id(), count);
      num_categories_selected->Increment();
    }
    catout.Close();

    // Clean up.
    for (auto &it : fact_lexicon.map) free(it.second.second);
  }
};

REGISTER_TASK_PROCESSOR("fact-lexicon-extractor", FactLexiconExtractor);

}  // namespace nlp
}  // namespace sling

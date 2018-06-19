#include <iostream>

#include "sling/base/clock.h"
#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/file/textmap.h"
#include "sling/frame/store.h"
#include "sling/frame/serialization.h"
#include "sling/nlp/kb/facts.h"
#include "sling/util/bloom.h"
#include "sling/util/sortmap.h"

DEFINE_string(kb, "local/data/e/wiki/kb.sling", "Knowledge base");
//DEFINE_string(names, "local/data/e/wiki/en/name-table.repo", "Name table");

using namespace sling;
using namespace sling::nlp;

class FactVocabularyExtractor {
 public:
  void Run() {
    LOG(INFO) << "Load knowledge base";
    Store commons;
    LoadStore(FLAGS_kb, &commons);

    // Resolve symbols.
    Names names;
    Name p_instance_of(names, "P31");
    Name p_item_category(names, "/w/item/category");
    Name n_item(names, "/w/item");
    Name n_wikimedia_category(names, "Q4167836");
    names.Bind(&commons);

    LOG(INFO) << "Initialize fact catalog";
    FactCatalog catalog;
    catalog.Init(&commons);
    commons.Freeze();

    // A Bloom filter is used for checking for singleton facts. It is used as
    // a fast and compact check for detecting if a fact is a new fact. The
    // probabilistic nature of the Bloom filter means that the fact instance
    // counts can be off by one.
    //BloomFilter filter(4 * (1LL << 30), 8);
    BloomFilter filter(4000000000LL, 4);

    LOG(INFO) << "Extract facts";
    Clock clock;
    clock.start();
    int64 num_items = 0;
    int64 num_facts = 0;
    int64 num_filtered = 0;
    SortableMap<Handle, int64, HandleHash> category_lexicon;
    SortableMap<int64, std::pair<int64, char *>> fact_lexicon;
    commons.ForAll([&](Handle handle) {
      Frame item(&commons, handle);
      if (!item.IsA(n_item)) return;

      // Skip categories.
      if (item.GetHandle(p_instance_of) == n_wikimedia_category) return;

      // Extract facts from item.
      Store store(&commons);
      //LOG(INFO) << "Item " << store.DebugString(handle);
      Facts facts(&catalog, &store);
      facts.Extract(handle);

      // Add facts to fact lexicon.
      for (Handle fact : facts.list()) {
        int64 fp = store.Fingerprint(fact);
        if (filter.add(fp)) {
          auto &entry = fact_lexicon[fp];
          if (entry.second == nullptr) {
            entry.second = strdup(ToText(&store, fact).c_str());
          }
          entry.first++;
        } else {
          num_filtered++;
        }
      }
      num_facts += facts.list().size();

      // Extract categories from item.
      for (const Slot &s : item) {
        if (s.name == p_item_category) {
          category_lexicon[s.value]++;
        }
      }

      num_items++;
      if (num_items % 1000000 == 0) {
        LOG(INFO) << num_items << " processed, "
                  << num_facts << " facts, "
                  << num_filtered << " filtered, "
                  << fact_lexicon.map.size() << " fact types";
      }
    });
    clock.stop();
    int num_singletons = 0;
    int64 string_bytes = 0;
    for (auto &it : fact_lexicon.map) {
      if (it.second.first == 1) num_singletons++;
      string_bytes += strlen(it.second.second);
    }

    LOG(INFO) << num_items << " items";
    LOG(INFO) << num_facts << " facts";
    LOG(INFO) << fact_lexicon.map.size() << " fact types";
    LOG(INFO) << num_singletons << " singletons";
    LOG(INFO) << string_bytes << " string bytes";
    LOG(INFO) << clock.secs() << " secs";

    LOG(INFO) << "Write top facts";
    fact_lexicon.sort();
    LOG(INFO) << fact_lexicon.array.size() << " facts in lexicon";
    TextMapOutput factout("/tmp/facts.map");
    int fact_threshold = 10;
    int num_facts_selected = 0;
    for (int i = fact_lexicon.array.size() - 1; i >= 0; --i) {
      Text fact(fact_lexicon.array[i]->second.second);
      int64 count = fact_lexicon.array[i]->second.first;
      if (count < fact_threshold) break;
      factout.Write(fact, count);
      num_facts_selected++;
    }
    factout.Close();
    LOG(INFO) << num_facts_selected << " facts selected";

    for (auto &it : fact_lexicon.map) free(it.second.second);

    LOG(INFO) << "Write top categories";
    category_lexicon.sort();
    LOG(INFO) << category_lexicon.array.size() << " categories";
    TextMapOutput catout("/tmp/categories.map");
    int category_threshold = 10;
    int num_categories = 0;
    for (int i = category_lexicon.array.size() - 1; i >= 0; --i) {
      Frame cat(&commons, category_lexicon.array[i]->first);
      int64 count = category_lexicon.array[i]->second;
      if (count < category_threshold) break;
      catout.Write(cat.Id(), count);
      num_categories++;
    }
    catout.Close();
    LOG(INFO) << num_categories << " categories selected";
  }
};

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  FactVocabularyExtractor extractor;
  extractor.Run();

#if 0
  Store commons;
  LoadStore(FLAGS_kb, &commons);
  FactCatalog catalog;
  catalog.Init(&commons);
  commons.Freeze();

  Handle helle = commons.Lookup("Q57652");
  Store store(&commons);
  Facts facts(&catalog, &store);
  facts.Extract(helle);

  std::cout << facts.list().size() << " facts\n";
  for (Handle fact : facts.list()) {
    std::cout << "fact " << ToText(&store, fact) << "\n";
  }
#endif

  return 0;
}


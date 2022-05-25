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

#include "sling/nlp/search/item-terms.h"

#include <unordered_set>

#include "sling/nlp/document/phrase-tokenizer.h"
#include "sling/task/frames.h"
#include "sling/task/task.h"
#include "sling/util/arena.h"
#include "sling/util/mutex.h"
#include "sling/util/unicode.h"

namespace sling {
namespace nlp {

// Build search terms vector for each item.
class SearchTermsBuilder : public task::FrameProcessor {
 public:
  void Startup(task::Task *task) override {
    // Get parameters.
    string lang = task->Get("language", "en");
    language_ = commons_->Lookup("/lang/" + lang);
    normalization_ = task->Get("normalization", "cln");

    // Set up phrase normalization.
    Normalization norm = ParseNormalization(normalization_);
    tokenizer_.set_normalization(norm);

    // Statistics.
    num_items_ = task->GetCounter("items");
    num_tokens_ = task->GetCounter("tokens");
    num_terms_ = task->GetCounter("terms");
  }

  void Process(Slice key, uint64 serial, const Frame &frame) override {
    // Repository only supports item ids up to length 255.
    if (key.size() > 0xFF) return;

    // Find all item names.
    Store *store = frame.store();
    std::unordered_set<uint64> terms;
    std::vector<uint64> tokens;
    for (const Slot &s : frame) {
      if (s.name != n_name_ && s.name != n_alias_) continue;

      // Skip names and aliases in foreign languages.
      Handle value = store->Resolve(s.value);
      if (!store->IsString(value)) continue;
      Handle lang = store->GetString(value)->qualifier();
      bool foreign = !lang.IsNil() && lang != language_;
      if (foreign) continue;

      // Get term fingerprints for name.
      Text name = store->GetString(value)->str();
      if (!UTF8::Valid(name.data(), name.size())) continue;
      tokenizer_.TokenFingerprints(name, &tokens);

      // Add token fingerprints to term vector.
      for (uint64 token : tokens) {
        if (token != 1) terms.insert(token);
      }
      num_tokens_->Increment(tokens.size());
    }
    num_items_->Increment();
    num_terms_->Increment(terms.size());

    // Create repository entry.
    MutexLock lock(&mu_);
    char *id = string_arena_.dup(key);
    uint64 *termlist = term_arena_.alloc(terms.size());
    uint64 *t = termlist;
    for (uint64 term : terms) *t++ = term;
    item_table_.push_back(new Item(id, termlist, terms.size()));
  }

  void Flush(task::Task *task) override {
    // Build phrase repository.
    Repository repository;

    // Add normalization flags to repository.
    repository.AddBlock("normalization", normalization_);

    // Write item term map.
    LOG(INFO) << "Build item term map";
    int num_items = item_table_.size();
    int num_buckets = (num_items + 32) / 32;
    repository.WriteMap("Term", &item_table_, num_buckets);

    // Write repository to file.
    const string &filename = task->GetOutput("repository")->resource()->name();
    CHECK(!filename.empty());
    LOG(INFO) << "Write item term repository to " << filename;
    repository.Write(filename);
    LOG(INFO) << "Repository done";

    // Clear collected data.
    for (auto *item : item_table_) delete item;
    item_table_.clear();
    term_arena_.clear();
    string_arena_.clear();
  }

 private:
  // Item entry with term list.
  struct Item : public RepositoryMapItem {
    // Initialize new term.
    Item(const char *id, uint64 *terms, int num_terms)
      : id(id), terms(terms), num_terms(num_terms) {}

    // Write item to repository.
    int Write(OutputBuffer *output) const override {
      uint8 idlen = strlen(id);
      output->Write(&idlen, sizeof(uint8));
      output->Write(id, idlen);

      output->Write(&num_terms, sizeof(uint32));
      output->Write(terms, num_terms * sizeof(uint64));

      return sizeof(uint8) + idlen +
             sizeof(uint32) + num_terms * sizeof(uint64);
    }

    // Use phrase fingerprint as the hash code.
    uint64 Hash() const override {
      return Fingerprint(id, strlen(id));
    }

    const char *id;
    uint64 *terms;
    uint32 num_terms;
  };

  // Language for search terms.
  Handle language_;

  // Term normalization.
  string normalization_;

  // Phrase tokenizer for computing phrase fingerprints.
  PhraseTokenizer tokenizer_;

  // Item table.
  std::vector<RepositoryMapItem *> item_table_;

  // Memory arenas.
  Arena<uint64> term_arena_;
  StringArena string_arena_;

  // Symbols.
  Name n_name_{names_, "name"};
  Name n_alias_{names_, "alias"};

  // Mutex for serializing access to repository.
  Mutex mu_;

  // Statistics.
  task::Counter *num_items_ = nullptr;
  task::Counter *num_tokens_ = nullptr;
  task::Counter *num_terms_ = nullptr;
};

REGISTER_TASK_PROCESSOR("search-terms-builder", SearchTermsBuilder);


}  // namespace nlp
}  // namespace sling

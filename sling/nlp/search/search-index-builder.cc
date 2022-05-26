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

#include "sling/string/text.h"
#include "sling/nlp/document/phrase-tokenizer.h"
#include "sling/nlp/search/search-dictionary.h"
#include "sling/task/frames.h"
#include "sling/task/task.h"
#include "sling/util/arena.h"
#include "sling/util/mutex.h"
#include "sling/util/unicode.h"

namespace sling {
namespace nlp {

// Build search index with item posting lists for each search term.
class SearchIndexBuilder : public task::FrameProcessor {
 public:
  void Startup(task::Task *task) override {
    // Get parameters.
    string lang = task->Get("language", "en");
    language_ = commons_->Lookup("/lang/" + lang);
    normalization_ = task->Get("normalization", "cln");

    // Set up phrase normalization.
    Normalization norm = ParseNormalization(normalization_);
    tokenizer_.set_normalization(norm);

    // Load search dictionary.
    LOG(INFO) << "Load search dictionary";
    const string &dictfn = task->GetInput("dictionary")->resource()->name();
    dictionary_.Load(dictfn);
    LOG(INFO) << "Dictionary loaded";

    // Statistics.
    num_items_ = task->GetCounter("items");
    num_tokens_ = task->GetCounter("tokens");
    num_terms_ = task->GetCounter("terms");
  }

  void Process(Slice key, uint64 serial, const Frame &frame) override {
    // Repository only supports item ids up to length 255.
    if (key.size() > 0xFF) return;

    auto *item = dictionary_.Find(key);
    if (item) num_items_->Increment();
  }

  void Flush(task::Task *task) override {
    // Build phrase repository.
    Repository repository;

    // Add normalization flags to repository.
    repository.AddBlock("normalization", normalization_);

    // Write repository to file.
    const string &filename = task->GetOutput("repository")->resource()->name();
    CHECK(!filename.empty());
    LOG(INFO) << "Write search dictionary repository to " << filename;
    repository.Write(filename);
    LOG(INFO) << "Repository done";

    // Clear collected data.
    term_arena_.clear();
    string_arena_.clear();
  }

 private:
  // Language for search terms.
  Handle language_;

  // Term normalization.
  string normalization_;

  // Phrase tokenizer for computing term fingerprints.
  PhraseTokenizer tokenizer_;

  // Search dictionary with search terms for items.
  SearchDictionary dictionary_;

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

REGISTER_TASK_PROCESSOR("search-index-builder", SearchIndexBuilder);

}  // namespace nlp
}  // namespace sling

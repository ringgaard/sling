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

#include <arpa/inet.h>  // for htonl

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

// Extract term lists for items.
class SearchIndexMapper : public task::FrameProcessor {
 public:
  typedef std::unordered_set<uint64> Terms;

  void Startup(task::Task *task) override {
    // Get parameters.
    string lang = task->Get("language", "en");
    language_ = commons_->Lookup("/lang/" + lang);
    normalization_ = task->Get("normalization", "cln");
    task->Fetch("buckets", &num_buckets_);

    // Get output channels.
    entities_ = task->GetSink("entities");
    terms_ = task->GetSink("terms");

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
    num_terms_ = task->GetCounter("terms");
  }

  void Process(Slice key, uint64 serial, const Frame &frame) override {
    // Find item in dictionary.
    // TODO remove
    auto *item = dictionary_.Find(key);
    if (item) num_items_->Increment();

    // Compute frequency count for item.
    int popularity = frame.GetInt(n_popularity_);
    int fanin = frame.GetInt(n_fanin_);
    int count = popularity + fanin;

    // Get entity id for item.
    uint32 entityid = OutputEntity(key, count);
    CHECK(entityid >= 0);

    // Collect search terms for item.
    Store *store = frame.store();
    Terms terms;
    for (const Slot &s : frame) {
        if (s.name == n_name_ || s.name == n_alias_) {
        // Skip names and aliases in foreign languages.
        Handle value = store->Resolve(s.value);
        if (!store->IsString(value)) continue;
        Handle lang = store->GetString(value)->qualifier();
        bool foreign = !lang.IsNil() && lang != language_;

        if (!foreign) {
          Text name = store->GetString(value)->str();
          Collect(&terms, name);
        }
      }
    }
    num_items_->Increment();
    num_terms_->Increment(terms.size());

    // Output search terms for item.
    for (uint64 term : terms) OutputTerm(entityid, term);
  }

  uint32 OutputEntity(Slice id, uint32 count) {
    MutexLock lock(&mu_);
    entities_->Send(new task::Message(id, Slice(&count, sizeof(uint32))));
    return next_entityid_++;
  }

  void OutputTerm(uint32 entityid, uint64 term) {
    uint32 b = htonl(term % num_buckets_);
    terms_->Send(new task::Message(
      Slice(&b, sizeof(uint32)),
      term,
      Slice(&entityid, sizeof(uint32))));
  }

  void Collect(Terms *terms, Text text) {
    if (!UTF8::Valid(text.data(), text.size())) return;
    std::vector<uint64> tokens;
    tokenizer_.TokenFingerprints(text, &tokens);
    for (uint64 token : tokens) {
      if (token != 1) terms->insert(token);
    }
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

  // Output channels.
  task::Channel *entities_ = nullptr;
  task::Channel *terms_ = nullptr;

  // Number of term buckets.
  int num_buckets_ = 1 << 20;

  // Next entity id.
  uint32 next_entityid_ = 0;

  // Symbols.
  Name n_name_{names_, "name"};
  Name n_alias_{names_, "alias"};
  Name n_popularity_{names_, "/w/item/popularity"};
  Name n_fanin_{names_, "/w/item/fanin"};

  // Statistics.
  task::Counter *num_items_ = nullptr;
  task::Counter *num_terms_ = nullptr;

  // Mutex for serializing access to entity ids.
  Mutex mu_;
};

REGISTER_TASK_PROCESSOR("search-index-mapper", SearchIndexMapper);

// Build search index with item posting lists for each search term.
class SearchIndexBuilder : public task::Processor {
 public:
  ~SearchIndexBuilder() {
    delete entity_index_;
    delete entity_items_;
  }

  void Start(task::Task *task) override {
    // Add normalization flags to repository.
    repository_.AddBlock("normalization", task->Get("normalization", "cln"));

    // Repository streams.
    entity_index_ = new OutputBuffer(repository_.AddBlock("EntityIndex"));
    entity_items_ = new OutputBuffer(repository_.AddBlock("EntityItems"));

    // Get input channels.
    entities_ = task->GetSource("entities");
    terms_ = task->GetSource("terms");
  }

  void Receive(task::Channel *channel, task::Message *message) override {
    if (channel == entities_) {
      // Write entity index entry.
      entity_index_->Write(&entity_offset_, sizeof(uint32));

      // Write count and id to entity entry.
      const Slice &entityid = message->key();
      const Slice &count = message->value();
      CHECK_LT(entityid.size(), 256);
      DCHECK_EQ(entityid.size(), sizeof(uint32));

      uint8 idlen = entityid.size();
      entity_items_->Write(count.data(), sizeof(uint32));
      entity_items_->Write(&idlen, sizeof(uint8));
      entity_items_->Write(entityid.data(), idlen);

      // Compute offset of next entry.
      entity_offset_ += sizeof(uint32) + sizeof(uint8) + idlen;
    }

    delete message;
  }

  void Done(task::Task *task) override {
    // Flush repository streams.
    entity_index_->Flush();
    entity_items_->Flush();

    // Write repository to file.
    const string &filename = task->GetOutput("repository")->resource()->name();
    CHECK(!filename.empty());
    LOG(INFO) << "Write search dictionary repository to " << filename;
    repository_.Write(filename);
    LOG(INFO) << "Repository done";
  }

 private:
  // Input channels.
  task::Channel *entities_ = nullptr;
  task::Channel *terms_ = nullptr;

  // Seach index repository.
  Repository repository_;

  // Output buffers for entity table.
  OutputBuffer *entity_index_ = nullptr;
  OutputBuffer *entity_items_ = nullptr;

  // Offset for next entity item.
  uint32 entity_offset_ = 0;

};

REGISTER_TASK_PROCESSOR("search-index-builder", SearchIndexBuilder);

}  // namespace nlp
}  // namespace sling

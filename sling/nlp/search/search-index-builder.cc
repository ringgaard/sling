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
#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include "sling/nlp/document/phrase-tokenizer.h"
#include "sling/nlp/search/search-dictionary.h"
#include "sling/nlp/search/search-config.h"
#include "sling/task/frames.h"
#include "sling/task/task.h"
#include "sling/string/text.h"
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
    // Read search index configuration.
    config_.Load(commons_, task->GetInputFile("config"));

    // Get output channels.
    entities_ = task->GetSink("entities");
    terms_ = task->GetSink("terms");

    // Load search dictionary.
    LOG(INFO) << "Load search dictionary";
    const string &dictfn = task->GetInput("dictionary")->resource()->name();
    dictionary_.Load(dictfn);
    LOG(INFO) << "Dictionary loaded";

    // Statistics.
    num_items_ = task->GetCounter("items");
    num_terms_ = task->GetCounter("terms");
    num_stopwords_ = task->GetCounter("stopwords");
  }

  void Process(Slice key, uint64 serial, const Frame &frame) override {
    // Skip non-entity items.
    Store *store = frame.store();
    for (const Slot &s : frame) {
      if (s.name == n_instance_of_) {
        Handle type = store->Resolve(s.value);
        if (config_.skipped(type)) return;
      }
    }

    // Compute frequency count for item.
    int popularity = frame.GetInt(n_popularity_);
    int fanin = frame.GetInt(n_fanin_);
    int count = popularity + fanin;

    // Get entity id for item.
    uint32 entityid = OutputEntity(key, count);
    CHECK(entityid >= 0);

    // Collect search terms for item.
    Terms terms;
    for (const Slot &s : frame) {
      // Check if properties should be indexed.
      Handle type = config_.index(s.name);
      if (type.IsNil()) continue;
      Handle value = store->Resolve(s.value);

      if (type == n_name_ || type == n_text_) {
        // Skip names in foreign languages.
        if (store->IsString(value)) {
          Handle lang = store->GetString(value)->qualifier();
          if (!config_.foreign(lang)) {
            Text name = store->GetString(value)->str();
            Collect(&terms, name);
          }
        }
      } else if (type == n_item_) {
        if (store->IsFrame(value)) {
          Text id = store->FrameId(value);
          const SearchDictionary::Item *item = dictionary_.Find(id);
          Collect(&terms, item);
        } else if (store->IsString(value)) {
          Text str = store->GetString(value)->str();
          Collect(&terms, str);
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
    uint32 b = htonl(term % config_.buckets());
    terms_->Send(new task::Message(
      Slice(&b, sizeof(uint32)),
      term,
      Slice(&entityid, sizeof(uint32))));
  }

  void Collect(Terms *terms, Text text) {
    if (!UTF8::Valid(text.data(), text.size())) return;
    std::vector<uint64> tokens;
    config_.tokenizer().TokenFingerprints(text, &tokens);
    for (uint64 token : tokens) {
      if (config_.stopword(token)) {
        num_stopwords_->Increment();
      } else {
        terms->insert(token);
      }
    }
  }

  void Collect(Terms *terms, const SearchDictionary::Item *item) {
    if (item == nullptr) return;
    const uint64 *t = item->terms();
    for (int i = 0; i < item->num_terms(); ++i) {
      terms->insert(*t++);
    }
  }

 private:
  // Search engine configuration.
  SearchConfiguration config_;

  // Search dictionary with search terms for items.
  SearchDictionary dictionary_;

  // Output channels.
  task::Channel *entities_ = nullptr;
  task::Channel *terms_ = nullptr;

  // Next entity id.
  uint32 next_entityid_ = 0;

  // Symbols.
  Name n_name_{names_, "name"};
  Name n_text_{names_, "text"};
  Name n_item_{names_, "item"};
  Name n_date_{names_, "date"};
  Name n_popularity_{names_, "/w/item/popularity"};
  Name n_fanin_{names_, "/w/item/fanin"};
  Name n_instance_of_{names_, "P31"};

  // Statistics.
  task::Counter *num_items_ = nullptr;
  task::Counter *num_terms_ = nullptr;
  task::Counter *num_stopwords_ = nullptr;

  // Mutex for serializing access to entity ids.
  Mutex mu_;
};

REGISTER_TASK_PROCESSOR("search-index-mapper", SearchIndexMapper);

// Build search index with item posting lists for each search term.
class SearchIndexBuilder : public task::Processor {
 public:
  ~SearchIndexBuilder() {
    ClearStreams();
  }

  void Start(task::Task *task) override {
    // Read search index configuration.
    Store store;
    SearchConfiguration config;
    config.Load(&store, task->GetInputFile("config"));
    num_buckets_ = config.buckets();

    // Add normalization flags to repository.
    repository_.AddBlock("normalization", config.normalization());

    // Add stopwords to repository.
    std::vector<uint64> stopwords;
    for (uint64 fp : config.stopwords()) {
      stopwords.push_back(fp);
    }
    repository_.AddBlock("stopwords",
                         stopwords.data(),
                         stopwords.size() * sizeof(uint64));

    // Repository streams.
    entity_index_ = AddStream("EntityIndex");
    entity_items_ = AddStream("EntityItems");
    term_buckets_ = AddStream("TermBuckets");
    term_items_ = AddStream("TermItems");

    // Get input channels.
    entities_ = task->GetSource("entities");
    terms_ = task->GetSource("terms");

    // Statistics.
    num_posting_lists_ = task->GetCounter("posting_lists");
    num_posting_entries_ = task->GetCounter("posting_entries");
  }

  void Receive(task::Channel *channel, task::Message *message) override {
    if (channel == entities_) {
      ProcessEntity(message->key(), message->value());
    } else if (channel == terms_) {
      ProcessTerm(message->serial(), message->value());
    }

    delete message;
  }

  void ProcessEntity(Slice entityid, Slice count) {
    // Write entity index entry.
    entity_index_->Write(&entity_offset_, sizeof(uint32));

    // Write count and id to entity entry.
    CHECK_LT(entityid.size(), 256);
    CHECK_EQ(count.size(), sizeof(uint32));
    uint8 idlen = entityid.size();
    entity_items_->Write(count.data(), sizeof(uint32));
    entity_items_->Write(&idlen, sizeof(uint8));
    entity_items_->Write(entityid.data(), idlen);

    // Compute offset of next entry.
    entity_offset_ += sizeof(uint32) + sizeof(uint8) + idlen;
  }

  void ProcessTerm(uint64 term, Slice entity) {
    // Parse input.
    CHECK_EQ(entity.size(), sizeof(uint32));
    int bucket = term % num_buckets_;
    uint32 entityid = *reinterpret_cast<const uint32 *>(entity.data());

    // Check for new term.
    if (term != current_term_) {
      FlushTerm();
      current_term_ = term;
    }

    // Update bucket table.
    while (next_bucket_ <= bucket) {
      term_buckets_->Write(&term_offset_, sizeof(uint64));
      next_bucket_++;
    }

    // Add new posting to term posting list.
    posting_list_.push_back(entityid);
  }

  void Done(task::Task *task) override {
    // Flush last term.
    if (!posting_list_.empty()) FlushTerm();

    // Flush buckets. We allocate one extra bucket to mark the end of the
    // term items.
    while (next_bucket_ <= num_buckets_) {
      term_buckets_->Write(&term_offset_, sizeof(uint64));
      next_bucket_++;
    }

    // Flush repository streams.
    for (auto *stream : streams_) stream->Flush();

    // Write repository.
    const string &filename = task->GetOutput("repository")->resource()->name();
    CHECK(!filename.empty());
    LOG(INFO) << "Write search dictionary repository to " << filename;
    repository_.Write(filename);
    LOG(INFO) << "Repository done";

    // Clean up.
    ClearStreams();
    posting_list_.clear();
  }

  void FlushTerm() {
    // Sort posting list.
    std::sort(posting_list_.begin(), posting_list_.end());

    // Write term posting list.
    uint32 size = posting_list_.size();
    term_items_->Write(&current_term_, sizeof(uint64));
    term_items_->Write(&size, sizeof(uint32));
    term_items_->Write(posting_list_.data(), size * sizeof(uint32));
    term_offset_ += sizeof(uint64) + (size + 1) * sizeof(uint32);

    posting_list_.clear();
    num_posting_lists_->Increment();
    num_posting_entries_->Increment(size);
  }

 private:
  OutputBuffer *AddStream(const string &name) {
    auto *stream = new OutputBuffer(repository_.AddBlock(name));
    streams_.push_back(stream);
    return stream;
  }

  void ClearStreams() {
    for (auto *stream : streams_) delete stream;
    streams_.clear();
  }

  // Input channels.
  task::Channel *entities_ = nullptr;
  task::Channel *terms_ = nullptr;

  // Seach index repository.
  Repository repository_;

  // Output buffers for entity table.
  OutputBuffer *entity_index_ = nullptr;
  OutputBuffer *entity_items_ = nullptr;
  OutputBuffer *term_buckets_ = nullptr;
  OutputBuffer *term_items_ = nullptr;
  std::vector<OutputBuffer *> streams_;

  // Number of term buckets.
  int num_buckets_ = 1 << 20;

  // Current bucket and term.
  int next_bucket_ = 0;
  uint64 current_term_ = 0;

  // Entities for current term.
  std::vector<uint32> posting_list_;

  // Offset for next entity item.
  uint32 entity_offset_ = 0;

  // Offset for next term entry.
  uint64 term_offset_ = 0;

  // Statistics.
  task::Counter *num_posting_lists_ = nullptr;
  task::Counter *num_posting_entries_ = nullptr;
};

REGISTER_TASK_PROCESSOR("search-index-builder", SearchIndexBuilder);

}  // namespace nlp
}  // namespace sling

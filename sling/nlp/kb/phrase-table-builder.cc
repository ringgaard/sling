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

#include <algorithm>
#include <string>
#include <unordered_map>

#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/file/buffered.h"
#include "sling/file/repository.h"
#include "sling/frame/object.h"
#include "sling/nlp/wiki/wiki.h"
#include "sling/string/numbers.h"
#include "sling/string/text.h"
#include "sling/task/frames.h"
#include "sling/task/task.h"
#include "sling/util/arena.h"
#include "sling/util/mutex.h"

namespace sling {
namespace nlp {

// Build phrase table repository from aliases.
class PhraseTableBuilder : public task::FrameProcessor {
 public:
  void Startup(task::Task *task) override {
    // Get parameters.
    task->Fetch("reliable_alias_sources", &reliable_alias_sources_);

    // Statistics.
    num_aliases_ = task->GetCounter("aliases");
    num_entities_ = task->GetCounter("entities");
    num_instances_ = task->GetCounter("instances");
  }

  void Process(Slice key, const Frame &frame) override {
    MutexLock lock(&mu_);
    Store *store = frame.store();

    // Get phrase fingerprint for alias cluster.
    uint64 fp;
    CHECK(safe_strtou64_base(key.data(), key.size(), &fp, 10));

    // Add new phrase to phrase table.
    Phrase *phrase = new Phrase(fp);
    phrase_table_.push_back(phrase);
    num_aliases_->Increment();

    // Get items for alias.
    for (const Slot &s : frame) {
      // Skip alias name.
      if (s.name == Handle::is()) continue;

      // Get index for entity.
      int index;
      Text id = store->FrameId(s.name);
      auto fe = entity_mapping_.find(id);
      if (fe == entity_mapping_.end()) {
        index = entity_table_.size();
        id = string_arena_.dup(id);
        entity_table_.emplace_back(id);
        num_entities_->Increment();
        entity_mapping_[id] = index;
      } else {
        index = fe->second;
      }

      // Add entity to phrase.
      Frame alias(store, s.value);
      int count = alias.GetInt(n_count_, 1);
      int sources = alias.GetInt(n_sources_, 0);
      int form = alias.GetInt(n_form_, 0);
      bool reliable = (sources & reliable_alias_sources_);
      phrase->entities.emplace_back(index, count, form, reliable);

      // Add alias count to entity frequency.
      entity_table_[index].count += count;
      num_instances_->Increment(count);
    }

    // Sort entities in decreasing order.
    std::sort(phrase->entities.begin(), phrase->entities.end(),
        [](const EntityPhrase &a, const EntityPhrase &b) {
          return a.count() > b.count();
        });
  }

  void Flush(task::Task *task) override {
    // Build phrase repository.
    Repository repository;

    // Add normalization flags to repository.
    const string &normalization = task->Get("normalization", "lcn");
    repository.AddBlock("normalization", normalization);

    // Write entity map.
    LOG(INFO) << "Build entity map";
    OutputBuffer entity_index_block(repository.AddBlock("EntityIndex"));
    OutputBuffer entity_item_block(repository.AddBlock("EntityItems"));
    uint32 offset = 0;
    for (Entity &entity : entity_table_) {
      // Write entity index entry.
      entity_index_block.Write(&offset, sizeof(uint32));

      // Write count and id to entity entry.
      CHECK_LT(entity.id.size(), 256);
      uint8 idlen = entity.id.size();
      entity_item_block.Write(&entity.count, sizeof(uint32));
      entity_item_block.Write(&idlen, sizeof(uint8));
      entity_item_block.Write(entity.id.data(), idlen);

      // Compute offset of next entry.
      offset += sizeof(uint32) + sizeof(uint8) + idlen;
    }
    entity_index_block.Flush();
    entity_item_block.Flush();

    // Write phrase map.
    LOG(INFO) << "Build phrase map";
    int num_phrases = phrase_table_.size();
    int num_buckets = (num_phrases + 32) / 32;
    repository.WriteMap("Phrase", &phrase_table_, num_buckets);

    // Write repository to file.
    const string &filename = task->GetOutput("repository")->resource()->name();
    CHECK(!filename.empty());
    LOG(INFO) << "Write phrase repository to " << filename;
    repository.Write(filename);
    LOG(INFO) << "Repository done";

    // Clear collected data.
    for (auto *p : phrase_table_) delete p;
    phrase_table_.clear();
    entity_table_.clear();
    entity_mapping_.clear();
    string_arena_.clear();
  }

 private:
  // Entity with id and frequency.
  struct Entity {
    Entity(Text id) : id(id) {}
    Text id;
    uint32 count = 0;
  };

  // Entity phrase with index and frequency. The count_and_flags field contains
  // the count in the lower 29 bit. Bit 29 and 30 contain the case form, and
  // bit 31 contains the reliable source flag.
  struct EntityPhrase {
    EntityPhrase() = default;
    EntityPhrase(int index, uint32 count, uint32 form, bool reliable)
        : index(index),
          count_and_flags(count | (form << 29) | (reliable ? (1 << 31) : 0)) {}
    uint32 index;
    uint32 count_and_flags;

    // Phrase frequency.
    int count() const { return count_and_flags & ((1 << 29) - 1); }

    // Alias reliability.
    bool reliable() const { return count_and_flags & (1 << 31); }

    // Phrase form.
    int form() const { return (count_and_flags >> 29) & 3; }
  };

  // Phrase with fingerprint and entity distribution.
  struct Phrase : public RepositoryMapItem {
    // Initialize new phrase.
    Phrase(uint64 fingerprint) : fingerprint(fingerprint) {}

    // Write phrase to repository.
    int Write(OutputBuffer *output) const override {
      output->Write(&fingerprint, sizeof(uint64));
      uint32 count = entities.size();
      output->Write(&count, sizeof(uint32));
      for (const EntityPhrase &ep : entities) {
        output->Write(&ep, sizeof(EntityPhrase));
      }
      return sizeof(uint64) + sizeof(uint32) + count * sizeof(EntityPhrase);
    }

    // Use phrase fingerprint as the hash code.
    uint64 Hash() const override { return fingerprint; }

    uint64 fingerprint;                  // phrase fingerprint
    std::vector<EntityPhrase> entities;  // list of entities for name phrase
  };

  // Symbols.
  Name n_count_{names_, "count"};
  Name n_form_{names_, "form"};
  Name n_sources_{names_, "sources"};

  // Reliable alias sources.
  int reliable_alias_sources_ =
    (1 << SRC_WIKIDATA_LABEL) |
    (1 << SRC_WIKIDATA_ALIAS) |
    (1 << SRC_WIKIDATA_NAME) |
    (1 << SRC_WIKIDATA_DEMONYM) |
    (1 << SRC_WIKIPEDIA_NAME);

  // Memory arena for strings.
  StringArena string_arena_;

  // Phrase table.
  std::vector<RepositoryMapItem *> phrase_table_;

  // Entity table with id and frequency count.
  std::vector<Entity> entity_table_;

  // Mapping of entity id to entity index in entity table.
  std::unordered_map<Text, int> entity_mapping_;

  // Statistics.
  task::Counter *num_entities_ = nullptr;
  task::Counter *num_aliases_ = nullptr;
  task::Counter *num_instances_ = nullptr;

  // Mutex for serializing access to repository.
  Mutex mu_;
};

REGISTER_TASK_PROCESSOR("phrase-table-builder", PhraseTableBuilder);

}  // namespace nlp
}  // namespace sling


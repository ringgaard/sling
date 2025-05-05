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
#include <vector>

#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/file/buffered.h"
#include "sling/file/repository.h"
#include "sling/task/frames.h"
#include "sling/task/task.h"
#include "sling/util/arena.h"
#include "sling/util/mutex.h"
#include "sling/util/unicode.h"

namespace sling {
namespace nlp {

// Build name table repository from aliases.
class NameTableBuilder : public task::FrameProcessor {
 public:
  void Startup(task::Task *task) override {
    // Set name normalization. Use phrase normalization for name table.
    normalization_ = ParseNormalization(task->Get("normalization", "lcpnDP"));

    // Statistics.
    num_aliases_ = task->GetCounter("aliases");
    num_names_ = task->GetCounter("names");
    num_entities_ = task->GetCounter("entities");
    num_instances_ = task->GetCounter("instances");
  }

  void Process(Slice key, uint64 serial, const Frame &frame) override {
    MutexLock lock(&mu_);
    Store *store = frame.store();

    // Get all entities for alias. Assume that all slots are entities for alias
    // except for the is: slot for the alias name.
    int num_entities = frame.size() - 1;
    EntityName *entities = entity_name_arena_.alloc(num_entities);
    int i = 0;
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

      // Add entity to name.
      Frame alias(store, s.value);
      int count = alias.GetInt(n_count_, 1);
      entities[i].index = index;
      entities[i].count = count;
      i++;
      num_names_->Increment();

      // Add alias count to entity frequency.
      entity_table_[index].count += count;
      num_instances_->Increment(count);
    }
    CHECK_EQ(i, num_entities);

    // Sort entities in decreasing order.
    std::sort(entities, entities + num_entities,
        [](const EntityName &a, const EntityName &b) {
          return a.count > b.count;
        });

    // Get normalized alias for cluster.
    Text alias = frame.GetText(Handle::is()).trim();
    string normalized;
    UTF8::Normalize(alias.data(), alias.size(), normalization_, &normalized);
    if (normalized.empty()) return;
    if (normalized.size() > 127) return;
    Text name = string_arena_.dup(normalized);

    // Add new entry to name table.
    name_table_.emplace_back(name, num_entities, entities);
    num_aliases_->Increment();
  }

  void Flush(task::Task *task) override {
    // Build name repository.
    Repository repository;

    // Sort names.
    LOG(INFO) << "Sort names";
    std::sort(name_table_.begin(), name_table_.end(),
        [](const NameEntry &a, const NameEntry &b) {
          return a.name < b.name;
        });

    // Add normalization flags to repository.
    string norm = NormalizationString(normalization_);
    repository.AddBlock("normalization", norm);

    // Get name repository blocks.
    OutputBuffer index_block(repository.AddBlock("Index"));
    OutputBuffer name_block(repository.AddBlock("Names"));
    OutputBuffer entity_block(repository.AddBlock("Entities"));

    // Write entity block.
    LOG(INFO) << "Build entity block";
    uint32 offset = 0;
    for (Entity &entity : entity_table_) {
      entity.offset = offset;

      // Write count and id to entity entry.
      CHECK_LT(entity.id.size(), 256);
      uint8 idlen = entity.id.size();
      entity_block.Write(&entity.count, sizeof(uint32));
      entity_block.Write(&idlen, sizeof(uint8));
      entity_block.Write(entity.id.data(), idlen);

      // Compute offset of next entry.
      offset += sizeof(uint32) + sizeof(uint8) + idlen;
    }
    entity_block.Flush();

    // Write name and index blocks.
    LOG(INFO) << "Build name and index blocks";
    offset = 0;
    for (const NameEntry &entry : name_table_) {
      // Write name offset to index.
      index_block.Write(&offset, sizeof(uint32));

      // Write name to name block.
      CHECK_LT(entry.name.size(), 256);
      uint8 namelen = entry.name.size();
      name_block.Write(&namelen, sizeof(uint8));
      name_block.Write(entry.name.data(), namelen);

      // Write entity list to name block.
      name_block.Write(&entry.num_entities, sizeof(uint32));
      for (int i = 0; i < entry.num_entities; ++i) {
        const EntityName &entity = entry.entities[i];
        name_block.Write(&entity_table_[entity.index].offset, sizeof(uint32));
        name_block.Write(&entity.count, sizeof(uint32));
      }

      // Compute offset of next entry.
      int arraylen = 2 * entry.num_entities * sizeof(uint32);
      offset += sizeof(uint8) + namelen + sizeof(uint32) + arraylen;
    }
    index_block.Flush();
    name_block.Flush();

    // Write repository to file.
    const string &filename = task->GetOutput("repository")->resource()->name();
    LOG(INFO) << "Write name repository to " << filename;
    repository.Write(filename);
    LOG(INFO) << "Repository done";

    // Clear collected data.
    name_table_.clear();
    entity_table_.clear();
    entity_mapping_.clear();
    entity_name_arena_.clear();
    string_arena_.clear();
  }

 private:
  // Entity with id and frequency.
  struct Entity {
    Entity(Text id) : id(id) {}
    Text id;
    uint32 count = 0;
    uint32 offset;
  };

  // Entity name with index and frequency.
  struct EntityName {
    uint32 index;
    uint32 count;
  };

  // Name entry with normalized phrase and list of entities.
  struct NameEntry {
    NameEntry(Text name, int num_entities, EntityName *entities)
      : name(name), num_entities(num_entities), entities(entities) {}
    Text name;
    uint32 num_entities;
    EntityName *entities;
  };

  // Symbols.
  Name n_count_{names_, "count"};

  // Text normalization flags.
  Normalization normalization_;

  // Memory arenas.
  Arena<EntityName> entity_name_arena_;
  StringArena string_arena_;

  // Name table.
  std::vector<NameEntry> name_table_;

  // Entity table with id and frequency count.
  std::vector<Entity> entity_table_;

  // Mapping of entity id to entity index in entity table.
  std::unordered_map<Text, int> entity_mapping_;

  // Statistics.
  task::Counter *num_names_ = nullptr;
  task::Counter *num_entities_ = nullptr;
  task::Counter *num_aliases_ = nullptr;
  task::Counter *num_instances_ = nullptr;

  // Mutex for serializing access to repository.
  Mutex mu_;
};

REGISTER_TASK_PROCESSOR("name-table-builder", NameTableBuilder);

}  // namespace nlp
}  // namespace sling

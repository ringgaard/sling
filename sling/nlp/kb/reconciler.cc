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

#include "sling/base/types.h"
#include "sling/frame/serialization.h"
#include "sling/task/frames.h"
#include "sling/task/reducer.h"
#include "sling/util/mutex.h"

namespace sling {
namespace nlp {

// Read items and reconcile the identifiers. The effect of this frame processor
// is largely implicit. The identifier cluster frames are read into the commons
// store. When each item is read into a local store by the frame processor, the
// mapped ids are automatically converted to the reconciled ids because of the
// identifier cluster frames in the commons store. The item is output with a
// key that is mapped in a similar manner.
class ItemReconciler : public task::FrameProcessor {
 public:
  void Startup(task::Task *task) override {
    // Read reconciler configuration.
    FileReader reader(commons_, task->GetInputFile("config"));
    Frame config = reader.Read().AsFrame();
    CHECK(config.valid());

    // Get item sources order.
    if (config.Has("sources")) {
      Array sources = config.Get("sources").AsArray();
      CHECK(sources.valid());
      for (int i = 0; i < sources.length(); ++i) {
        source_order_.push_back(sources.get(i));
      }
    }

    // Get property inversions.
    if (config.Has("inversions")) {
      Frame inversions = config.Get("inversions").AsFrame();
      CHECK(inversions.valid());
      for (const Slot &slot : inversions) {
        inversion_map_[slot.name] = slot.value;
      }
    }

    // Statistics.
    num_mapped_ids_ = task->GetCounter("mapped_ids");
    num_inverse_properties_ = task->GetCounter("inverse_properties");
  }

  void Process(Slice key, const Frame &frame) override {
    // Lookup the key in the store to get the reconciled id for the frame.
    Store *store = frame.store();
    Text id = key;
    if (id.empty()) id = frame.Id();
    CHECK(!id.empty());
    Handle mapped = commons_->LookupExisting(id);
    if (!mapped.IsNil()) {
      id = commons_->FrameId(mapped);
      num_mapped_ids_->Increment();
    }

    // Remove all id slots.
    if (frame.Has(Handle::id())) {
      Builder b(frame);
      b.Delete(Handle::id());
      b.Update();
    }

    // Output inverted property frames.
    for (const Slot &slot : frame) {
      // Check for inverted property.
      auto f = inversion_map_.find(slot.name);
      if (f == inversion_map_.end()) continue;
      Handle inverse_property = f->second;

      // Do not invert non-items and self-relations.
      Handle target = store->Resolve(slot.value);
      if (!target.IsRef()) continue;
      Text target_id = store->FrameId(target);
      if (target_id.empty()) continue;

      // Output inverted property frame.
      Builder inverted(store);
      inverted.AddLink(inverse_property, id);
      Frame fi = inverted.Create();
      Output(target_id, ItemSourceOrder(fi), fi);
      num_inverse_properties_->Increment();
    }

    // Output frame with the reconciled id as key.
    Output(id, ItemSourceOrder(frame), frame);
  }

 private:
  // Get source order for frame.
  int ItemSourceOrder(const Frame &frame) {
    for (int i = 0; i < source_order_.size(); ++i) {
      if (frame.Has(source_order_[i])) return i;
    }
    return source_order_.size();
  }

  // Item source ordering.
  std::vector<Handle> source_order_;

  // Property inversion map.
  HandleMap<Handle> inversion_map_;

  // Statistics.
  task::Counter *num_mapped_ids_ = nullptr;
  task::Counter *num_inverse_properties_ = nullptr;
};

REGISTER_TASK_PROCESSOR("item-reconciler", ItemReconciler);

// Set of item statements implemented as a hash table for fast checking of
// duplicates.
class Statements {
 public:
  Statements(Store *store) : store_(store), slots_(store) {
    limit_ = INITIAL_CAPACITY;
    mask_ = limit_ - 1;
    size_ = 0;
    slots_.resize(limit_);
  }

  // Ensure capacity for inserting up to 'n' statements.
  void Ensure(int n) {
    // Check if there is enough space with a fill factor of 50%.
    int needed = (size_ + n) * 2;
    if (needed <= limit_) return;

    // Expand hash table.
    Slots slots(store_);
    slots.swap(slots_);
    while (limit_ < needed) limit_ *= 2;
    mask_ = limit_ - 1;
    slots_.resize(limit_);
    for (int i = 0; i < slots.size(); ++i) {
      int pos = NameHash(slots[i].name) & mask_;
      for (;;) {
        Slot &s = slots_[pos];
        if (s.name.IsNil()) {
          s = slots[i];
          break;
        }
        pos = (pos + 1) & mask_;
      }
    }
  }

  // Check if unqualified statement is in table.
  bool Has(Handle name, Handle value) {
    int pos = NameHash(name) & mask_;
    for (;;) {
      Slot &s = slots_[pos];
      if (s.name == name && store_->Equal(s.value, value)) {
        // Match found.
        return true;
      } else if (s.name.IsNil()) {
        return false;
      }
      pos = (pos + 1) & mask_;
    }
  }

  // Insert statement. Return false if the statement is already in the table.
  bool Insert(Handle name, Handle value) {
    int pos = NameHash(name) & mask_;
    for (;;) {
      Slot &s = slots_[pos];
      if (s.name == name && store_->Equal(s.value, value)) {
        // Match found.
        return false;
      } else if (s.name.IsNil()) {
        // Insert new slot.
        s.name = name;
        s.value = value;
        size_++;
        return true;
      }
      pos = (pos + 1) & mask_;
    }
  }

 private:
  // Initial size for hash hable. Must be power of two.
  static const uint64 INITIAL_CAPACITY = 1024;

  // Compute hash for name.
  static Word NameHash(Handle name) {
    return name.raw() >> Handle::kTagBits;
  }

  // Store for table.
  Store *store_;

  // Hash table with linear probing.
  Slots slots_;
  uint64 size_;
  uint64 limit_;
  uint64 mask_;
};

// Merge items with the same ids.
class ItemMerger : public task::Reducer {
 public:
  void Start(task::Task *task) override {
    task::Reducer::Start(task);

    // Statistics.
    num_orig_statements_ = task->GetCounter("original_statements");
    num_final_statements_ = task->GetCounter("final_statements");
    num_dup_statements_ = task->GetCounter("duplicate_statements");
    num_pruned_statements_ = task->GetCounter("pruned_statements");
    num_merged_items_ = task->GetCounter("merged_items");
  }

  void Reduce(const task::ReduceInput &input) override {
    // Create frame with reconciled id.
    Store store;
    Handle id = store.Lookup(input.key());
    Builder builder(&store);
    builder.AddId(id);

    // Merge all item sources.
    Statements statements(&store);
    for (task::Message *message : input.messages()) {
      // Decode item.
      Frame item = DecodeMessage(&store, message);
      num_orig_statements_->Increment(item.size());

      // Since the merged frames are anonymous, self-references need to be
      // updated to the reconciled frame.
      Handle self = item.handle();
      item.TraverseSlots([self, id](Slot *s) {
        if (s->name == self) s->name = id;
        if (s->value == self) s->value = id;
      });

      // Add new statements skipping duplicates.
      statements.Ensure(item.size());
      for (const Slot &s : item) {
        if (s.name == Handle::is()) continue;
        if (statements.Insert(s.name, s.value)) {
         builder.Add(s.name, s.value);
        } else {
          num_dup_statements_->Increment();
        }
      }
    }

    // Remove unqualified statements which have qualified counterparts.
    bool prune = false;
    for (int i = 0; i < builder.size(); ++i) {
      // Check if statement is qualified.
      Handle value = builder[i].value;
      Handle resolved = store.Resolve(value);
      if (value == resolved) continue;

      // Check if there is an unqualifed counterpart.
      Handle property = builder[i].name;
      if (statements.Has(property, resolved)) {
        // Remove unqualifed counterpart.
        for (int j = 0; j < builder.size(); ++j) {
          Slot &s = builder[j];
          if (s.name == property && store.Equal(s.value, resolved)) {
            // Mark statement for deletion.
            s.name = Handle::nil();
            prune = true;
            num_pruned_statements_->Increment();
            break;
          }
        }
      }
    }
    if (prune) builder.Prune();

    // Output merged frame for item.
    Frame merged = builder.Create();
    Output(input.shard(), task::CreateMessage(input.key(), merged));
    num_merged_items_->Increment();
    num_final_statements_->Increment(merged.size());

    // Add properties to property catalog.
    if (merged.IsA("/w/property")) {
      MutexLock lock(&mu_);
      string pid = merged.Id().str();
      properties_.push_back(pid);
    }
  }

  // Output property catalog.
  void Done(task::Task *task) override {
    Store store;
    Builder catalog(&store);
    catalog.AddId("/w/entity");
    catalog.AddIs("schema");
    catalog.Add("name", "Wikidata entity");
    catalog.AddLink("family", "/schema/wikidata");
    for (const string &id : properties_) {
      catalog.AddLink("role", id);
    }
    Output(0, task::CreateMessage(catalog.Create()));
  }

 private:
  // Property ids.
  std::vector<string> properties_;
  Mutex mu_;

  // Statistics.
  task::Counter *num_orig_statements_ = nullptr;
  task::Counter *num_final_statements_ = nullptr;
  task::Counter *num_dup_statements_ = nullptr;
  task::Counter *num_pruned_statements_ = nullptr;
  task::Counter *num_merged_items_ = nullptr;
};

REGISTER_TASK_PROCESSOR("item-merger", ItemMerger);

}  // namespace nlp
}  // namespace sling


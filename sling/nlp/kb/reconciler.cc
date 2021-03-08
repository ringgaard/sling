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

// Merge items with the same ids.
class ItemMerger : public task::Reducer {
 public:
  void Start(task::Task *task) override {
    task::Reducer::Start(task);

    // Statistics.
    num_merged_items_ = task->GetCounter("merged_items");
  }

  void Reduce(const task::ReduceInput &input) override {
    // Create frame with reconciled id.
    Store store;
    Handle id = store.Lookup(input.key());
    Builder builder(&store);
    builder.AddId(id);

    // Merge all items. Since the merged frames are anonymous, self-references
    // need to be updated to the reconciled frame.
    for (task::Message *message : input.messages()) {
      Frame item = DecodeMessage(&store, message);
      Handle h = item.handle();
      item.TraverseSlots([h, id](Slot *s) {
        if (s->name == h) s->name = id;
        if (s->value == h) s->value = id;
      });
      builder.AddFrom(item);
    }

    // Output merged frame for item.
    Frame merged = builder.Create();
    Output(input.shard(), task::CreateMessage(input.key(), merged));
    num_merged_items_->Increment();
  }

 private:
  // Statistics.
  task::Counter *num_merged_items_ = nullptr;
};

REGISTER_TASK_PROCESSOR("item-merger", ItemMerger);

}  // namespace nlp
}  // namespace sling


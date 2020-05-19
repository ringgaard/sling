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
    // Statistics.
    num_mapped_ids_ = task->GetCounter("mapped_ids");
  }

  void Process(Slice key, const Frame &frame) override {
    // Lookup the key in the store to get the reconciled id for the frame.
    Handle mapped = commons_->LookupExisting(key);
    Text id = key;
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

    // Output frame with the reconciled id as key.
    Output(id, frame);
  }

 private:
  // Statistics.
  task::Counter *num_mapped_ids_ = nullptr;
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
    Builder builder(&store);
    builder.AddId(input.key());

    // Merge all items.
    for (task::Message *message : input.messages()) {
      Frame item = DecodeMessage(&store, message);
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


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
#include "sling/task/accumulator.h"
#include "sling/task/frames.h"

namespace sling {
namespace nlp {

// Collect fact targets from items and output aggregate target counts.
class ItemFaninMapper : public task::FrameProcessor {
 public:
  void Startup(task::Task *task) override {
    accumulator_.Init(output(), task->Get("buckets", 1 << 20));
    system_properties_.add(Handle::id());
    system_properties_.add(Handle::is());
    system_properties_.add(Handle::isa());
    system_properties_.add(commons_->Lookup("name"));
    system_properties_.add(commons_->Lookup("alias"));
    system_properties_.add(commons_->Lookup("description"));
  }

  void Process(Slice key, const Frame &frame) override {
    // Accumulate fact properties and value counts for the item.
    Store *store = frame.store();
    for (const Slot &slot : frame) {
      if (slot.name == Handle::id()) continue;
      if (slot.name == Handle::isa()) continue;

      // Add slot name.
      if (store->IsFrame(slot.name)) Add(store, slot.name);

      // Add slot value.
      Handle value = store->Resolve(slot.value);
      if (!store->IsFrame(value)) continue;
      if (value == slot.value) {
        Add(store, value);
      } else {
        Frame qualifiers(store, value);
        for (const Slot &qslot : qualifiers) {
          if (store->IsFrame(qslot.name)) Add(store, qslot.name);
          Handle qvalue = store->Resolve(qslot.value);
          if (store->IsFrame(qvalue)) Add(store, qvalue);
        }
      }
    }
  }

  void Add(Store *store, Handle target) {
    if (system_properties_.has(target)) return;
    Text id = store->FrameId(target);
    if (!id.empty()) accumulator_.Increment(id);
  }

  void Flush(task::Task *task) override {
    accumulator_.Flush();
  }

 private:
  // Accumulator for fanin counts.
  task::Accumulator accumulator_;

  // Special properties where fanin is not computed.
  HandleSet system_properties_;
};

REGISTER_TASK_PROCESSOR("item-fanin-mapper", ItemFaninMapper);

// Aggregate fan-in for each item.
class ItemFaninReducer : public task::SumReducer {
 public:
  void Aggregate(int shard, const Slice &key, uint64 sum) override {
    // Output fan-in for item.
    Store store;
    Builder b(&store);
    int fanin = sum;
    b.Add("/w/item/fanin", fanin);
    Output(shard, task::CreateMessage(key, b.Create()));
  }
};

REGISTER_TASK_PROCESSOR("item-fanin-reducer", ItemFaninReducer);

}  // namespace nlp
}  // namespace sling


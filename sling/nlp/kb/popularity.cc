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
class FactTargetExtractor : public task::FrameProcessor {
 public:
  void Startup(task::Task *task) override {
    accumulator_.Init(output());
  }

  void Process(Slice key, const Frame &frame) override {
    // Accumulate fact targets for the item.
    Store *store = frame.store();
    for (const Slot &slot : frame) {
      if (slot.name == Handle::isa()) continue;
      if (slot.name == n_lang_) continue;

      Handle target = store->Resolve(slot.value);
      if (!store->IsFrame(target)) continue;

      Text id = store->FrameId(target);
      if (id.empty()) continue;

      accumulator_.Increment(id);
    }
  }

  void Flush(task::Task *task) override {
    accumulator_.Flush();
  }

 private:
  // Accumulator for fanin counts.
  task::Accumulator accumulator_;

  // Symbols.
  Name n_lang_{names_, "lang"};
};

REGISTER_TASK_PROCESSOR("fact-target-extractor", FactTargetExtractor);

// Sum item popularity and output popularity frame for each item.
class ItemPopularityReducer : public task::SumReducer {
 public:
  void Aggregate(int shard, const Slice &key, uint64 sum) override {
    // Output popularity frame for item.
    Store store;
    Builder b(&store);
    int popularity = sum;
    b.Add("/w/item/popularity", popularity);
    Output(shard, task::CreateMessage(key, b.Create()));
  }
};

REGISTER_TASK_PROCESSOR("item-popularity-reducer", ItemPopularityReducer);

}  // namespace nlp
}  // namespace sling


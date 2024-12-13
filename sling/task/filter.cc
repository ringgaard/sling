// Copyright 2024 Ringgaard Research ApS
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

#include <string>
#include <unordered_set>

#include "sling/base/types.h"
#include "sling/file/textmap.h"
#include "sling/task/task.h"

namespace sling {
namespace task {

// Discard messages by keys.
class DiscardTask : public Processor {
 public:
  void Start(Task *task) override {
    // Get output channel.
    output_ = task->GetSink("output");
    CHECK(output_ != nullptr) << "Output channel missing";

    // Read filter list(s).
    TextMapInput filters(task->GetInputFiles("discard"));
    while (filters.Next()) {
      discard_.insert(filters.key());
    }

    // Statistics.
    num_discarded_ = task->GetCounter("messages_discarded");
    LOG(INFO) << discard_.size() << " filtered keys";
  }

  void Receive(Channel *channel, Message *message) override {
    // Discard if key is in filter list.
    string key = message->key().str();
    if (discard_.count(key) > 0) {
      num_discarded_->Increment();
      delete message;
      return;
    }

    // Output message on output channel.
    output_->Send(message);
  }

 private:
  // Output channel.
  Channel *output_ = nullptr;

  // Discarded keys.
  std::unordered_set<string> discard_;

  // Statistics.
  Counter *num_discarded_ = nullptr;
};

REGISTER_TASK_PROCESSOR("discard", DiscardTask);

}  // namespace task
}  // namespace sling

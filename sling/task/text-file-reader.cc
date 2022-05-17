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

#include <string>

#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/stream/file-input.h"
#include "sling/task/process.h"
#include "sling/task/task.h"

namespace sling {
namespace task {

// Read text file and output lines to channel.
class TextFileReader : public Process {
 public:
  // Process input file.
  void Run(Task *task) override {
    // Get input files.
    auto inputs = task->GetInputs("input");

    // Get output channel.
    Channel *output = task->GetSink("output");
    if (output == nullptr) {
      LOG(ERROR) << "No output channel";
      return;
    }

    // Statistics counters.
    Counter *lines_read = task->GetCounter("text_lines_read");
    Counter *bytes_read = task->GetCounter("text_bytes_read");

    // Read input file(s).
    int buffer_size = task->Get("buffer_size", 1 << 16);
    int64 max_lines = task->Get("max_lines", 0);
    int64 num_lines = 0;
    for (Binding *input : inputs) {
      // Open input file.
      FileInput file(input->resource()->name(), buffer_size);
      uint64 serial = input->resource()->serial();

      // Read lines from file and output to output channel.
      string line;
      while (file.ReadLine(&line)) {
        // Update stats.
        lines_read->Increment();
        bytes_read->Increment(line.size());

        // Send message with line to output channel.
        output->Send(new Message(Slice(), serial, Slice(line)));

        // Stop when max lines reached.
        if (max_lines > 0 && ++num_lines == max_lines) break;
      }
    }

    // Close output channel.
    output->Close();
  }
};

REGISTER_TASK_PROCESSOR("text-file-reader", TextFileReader);

}  // namespace task
}  // namespace sling


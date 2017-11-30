#include <string>

#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/stream/file-input.h"
#include "sling/task/process.h"
#include "sling/task/task.h"

namespace sling {
namespace task {

// Read text map file and output lines to channel.
class TextMapReader : public Process {
 public:
  // Process input file.
  void Run(Task *task) override {
    // Get input file.
    Binding *input = task->GetInput("input");
    if (input == nullptr) {
      LOG(ERROR) << "No input resource";
      return;
    }

    // Get output channel.
    Channel *output = task->GetSink("output");
    if (output == nullptr) {
      LOG(ERROR) << "No output channel";
      return;
    }

    // Open input file.
    int buffer_size = task->Get("buffer_size", 1 << 16);
    FileInput file(input->resource()->name(), buffer_size);

    // Statistics counters.
    Counter *invalid_map_lines = task->GetCounter("invalid_map_lines");

    // Read lines from file and output to output channel.
    string line;
    while (file.ReadLine(&line)) {
      // Find delimiter.
      size_t split = line.find('\t');
      if (split == -1) {
        // Missing delimiter.
        invalid_map_lines->Increment();
        output->Send(new Message(Slice(), Slice(line)));
      } else {
        // Send message with split into key and value to output channel.
        Slice key(line.data(), split);
        Slice value(line.data() + split + 1, line.size() - split - 1);
        output->Send(new Message(key, value));
      }
    }

    // Close output channel.
    output->Close();
  }
};

REGISTER_TASK_PROCESSOR("text-map-reader", TextMapReader);

}  // namespace task
}  // namespace sling


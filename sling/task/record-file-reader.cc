#include <string>

#include "sling/base/logging.h"
#include "sling/file/recordio.h"
#include "sling/task/process.h"
#include "sling/task/task.h"

namespace sling {
namespace task {

// Read records from record file and output to channel.
class RecordFileReader : public Process {
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
    RecordReader reader(input->resource()->name());

    // Statistics counters.
    Counter *records_read = task->GetCounter("records_read");
    Counter *key_bytes_read = task->GetCounter("key_bytes_read");
    Counter *value_bytes_read = task->GetCounter("value_bytes_read");

    // Read records from file and output to output channel.
    Record record;
    while (!reader.Done()) {
      // Read record.
      CHECK(reader.Read(&record))
          << ", file: " << input->resource()->name()
          << ", position: " << reader.Tell();

      // Update stats.
      records_read->Increment();
      key_bytes_read->Increment(record.key.size());
      value_bytes_read->Increment(record.value.size());

      // Send message with record to output channel.
      Message *message = new Message(record.key, record.value);
      output->Send(message);
    }

    // Close reader.
    CHECK(reader.Close());

    // Close output channel.
    output->Close();
  }
};

REGISTER_TASK_PROCESSOR("record-file-reader", RecordFileReader);

}  // namespace task
}  // namespace sling


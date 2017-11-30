#include <string>
#include <vector>

#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/stream/file-input.h"
#include "sling/task/process.h"
#include "sling/task/task.h"
#include "sling/web/web-archive.h"

namespace sling {

using namespace task;

// Read WARC files and output web pages to channel. The message key is the WARC
// header and the message value is the web page content.
class WARCReader : public Process {
 public:
  // Process input files.
  void Run(Task *task) override {
    // Get input files.
    std::vector<Binding *> inputs = task->GetInputs("input");
    if (inputs.empty()) {
      LOG(ERROR) << "No input files";
      return;
    }

    // Get output channel.
    Channel *output = task->GetSink("output");
    if (output == nullptr) {
      LOG(ERROR) << "No output channel";
      return;
    }

    // Get parameters.
    int buffer_size = task->Get("buffer_size", 1 << 16);
    int max_warc_files = task->Get("max_warc_files", -1);
    string warc_type = task->Get("warc_type", "");

    // Statistics counters.
    Counter *files_read = task->GetCounter("warc_files_read");
    Counter *records_read = task->GetCounter("warc_records_read");
    Counter *bytes_read = task->GetCounter("warc_bytes_read");

    // Process all input files.
    int num_warc_files = 0;
    for (Binding *input : inputs) {
      // Open WARC file.
      VLOG(1) << "Read WARC file: " << input->resource()->name();
      WARCFile warc(input->resource()->name(), buffer_size);

      // Process all blocks in web archive.
      while (warc.Next()) {
        // Check WARC record type.
        if (!warc_type.empty() && warc.type() != warc_type) {
          continue;
        }

        // Create message where the key is the WARC header and the value is the
        // record content.
        int key_size = warc.headers().buffer().size();
        int value_size = warc.content_length();
        Message *message = new Message(key_size, value_size);

        // Copy WARC header to message key.
        memcpy(message->key_buffer()->data(),
               warc.headers().buffer().data(),
               key_size);

        // Copy WARC record content to message value.
        char *value = message->value_buffer()->data();
        const void *data;
        int size;
        while (warc.content()->Next(&data, &size)) {
          memcpy(value, data, size);
          value += size;
        }

        // Send message with WARC record to output channel.
        output->Send(message);

        records_read->Increment();
        bytes_read->Increment(value_size);
      }

      // Stop if we have reached the maximum number of files.
      files_read->Increment();
      if (++num_warc_files >= max_warc_files && max_warc_files > 0) break;
    }

    // Close output channel.
    output->Close();
  }
};

REGISTER_TASK_PROCESSOR("warc-reader", WARCReader);

}  // namespace sling


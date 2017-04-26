#include "base/logging.h"
#include "file/recordio.h"
#include "task/task.h"
#include "util/mutex.h"

namespace sling {
namespace task {

// Write incoming messages to record file.
class RecordFileWriter : public Processor {
 public:
  ~RecordFileWriter() override { delete writer_; }

  void Init(Task *task) override {
    // Get output file.
    Binding *output = task->GetOutput("output");
    if (output == nullptr) {
      LOG(ERROR) << "Output missing";
      return;
    }

    // Open record file writer.
    writer_ = new RecordWriter(output->resource()->name());
  }

  void Receive(Channel *channel, Message *message) override {
    MutexLock lock(&mu_);

    // Write message to record file.
    CHECK_OK(writer_->Write(message->key(), message->value()));
    delete message;
  }

  void Done(Task *task) override {
    MutexLock lock(&mu_);

    // Close writer.
    if (writer_ != nullptr) {
      CHECK_OK(writer_->Close());
      delete writer_;
      writer_ = nullptr;
    }
  }

 private:
  // Record writer for writing to output.
  RecordWriter *writer_ = nullptr;

  // Mutex for record writer.
  Mutex mu_;
};

REGISTER_TASK_PROCESSOR("record-file-writer", RecordFileWriter);

}  // namespace task
}  // namespace sling


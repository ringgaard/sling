#include "base/logging.h"
#include "stream/file.h"
#include "stream/output.h"
#include "task/task.h"
#include "util/mutex.h"

namespace sling {
namespace task {

// Write incoming messages to text map file.
class TextMapWriter : public Processor {
 public:
  ~TextMapWriter() override {
    delete writer_;
    delete stream_;
  }

  void Init(Task *task) override {
    // Get output file.
    Binding *output = task->GetOutput("output");
    if (output == nullptr) {
      LOG(ERROR) << "Output missing";
      return;
    }

    // Open file output stream.
    stream_ = new FileOutputStream(
        output->resource()->name(),
        task->Get("buffer_size", 1 << 16));
    writer_ = new Output(stream_);
  }

  void Receive(Channel *channel, Message *message) override {
    MutexLock lock(&mu_);

    // Write message key and value to text map file.
    writer_->Write(message->key().data(), message->key().size());
    writer_->WriteChar('\t');
    writer_->Write(message->value().data(), message->value().size());
    writer_->WriteChar('\n');
    delete message;
  }

  void Done(Task *task) override {
    MutexLock lock(&mu_);

    // Close writer.
    delete writer_;
    writer_ = nullptr;

    // Close output file.
    if (stream_ != nullptr) {
      CHECK(stream_->Close());
      delete stream_;
      stream_ = nullptr;
    }
  }

 private:
  // File output stream for writing to output.
  FileOutputStream *stream_ = nullptr;

  // Output buffer.
  Output *writer_ = nullptr;

  // Mutex for serializing writes.
  Mutex mu_;
};

REGISTER_TASK_PROCESSOR("text-map-writer", TextMapWriter);

}  // namespace task
}  // namespace sling


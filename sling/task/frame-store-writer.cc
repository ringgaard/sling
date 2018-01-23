#include "sling/base/logging.h"
#include "sling/frame/encoder.h"
#include "sling/frame/object.h"
#include "sling/stream/file.h"
#include "sling/task/frames.h"
#include "sling/task/task.h"
#include "sling/util/mutex.h"

namespace sling {
namespace task {

// Decode all input messages as frames and save them into a frame store.
class FrameStoreWriter : public Processor {
 public:
  FrameStoreWriter() { options_.symbol_rebinding = true; }
  ~FrameStoreWriter() { delete store_; }

  void Start(Task *task) override {
    // Create store.
    store_ = new Store(&options_);

    // Suppressing garbage collection can make store updates faster at the
    // expense of potentially larger memory usage.
    if (task->Get("suppress_gc", true)) {
      store_->LockGC();
    }
  }

  void Receive(Channel *channel, Message *message) override {
    MutexLock lock(&mu_);

    // Read frame into store.
    DecodeMessage(store_, message);
    delete message;
  }

  void Done(Task *task) override {
    // Get output file name.
    Binding *file = task->GetOutput("output");
    CHECK(file != nullptr);

    // Compact store.
    store_->CoalesceStrings();
    store_->GC();

    // Save store to output file.
    LOG(INFO) << "Saving store to " << file->resource()->name();
    FileOutputStream stream(file->resource()->name());
    Output output(&stream);
    Encoder encoder(store_, &output);
    encoder.set_shallow(true);
    encoder.EncodeAll();
    output.Flush();
    CHECK(stream.Close());

    // Delete store.
    delete store_;
    store_ = nullptr;
  }

 private:
  // Frame store.
  Store *store_ = nullptr;

  // Options for frame store.
  Store::Options options_;

  // Mutex for serializing access to store.
  Mutex mu_;
};

REGISTER_TASK_PROCESSOR("frame-store-writer", FrameStoreWriter);

}  // namespace task
}  // namespace sling


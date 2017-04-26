#include "base/logging.h"
#include "frame/encoder.h"
#include "frame/object.h"
#include "stream/file.h"
#include "task/frames.h"
#include "task/task.h"
#include "util/mutex.h"

namespace sling {
namespace task {

// Decode all input messages as frames and save them into a frame store.
class FrameStoreBuilder : public Processor {
 public:
  FrameStoreBuilder() { options_.symbol_rebinding = true; }
  ~FrameStoreBuilder() { delete store_; }

  void Start(Task *task) override {
    // Create store.
    store_ = new Store(&options_);
  }

  void Receive(Channel *channel, Message *message) override {
    MutexLock lock(&mu_);

    // Read frame into store.
    DecodeMessage(store_, message);
    delete message;
  }

  void Done(Task *task) override {
    // Get output file name.
    Binding *file = task->GetOutput("store");
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

REGISTER_TASK_PROCESSOR("frame-store-builder", FrameStoreBuilder);

}  // namespace task
}  // namespace sling


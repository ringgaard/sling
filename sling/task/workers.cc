#include "sling/task/task.h"
#include "sling/util/threadpool.h"

namespace sling {
namespace task {

// Create a pool of worker threads and distribute the incoming messages to
// the output channel using the worker threads. This adds parallelism to the
// processing of the message stream.
class Workers : public Processor {
 public:
  ~Workers() override { delete pool_; }

  void Start(Task *task) override {
    // Get output port.
    output_ = task->GetSink("output");

    // Get worker pool parameters.
    int num_workers = task->Get("worker_threads", 5);
    int queue_size = task->Get("queue_size", num_workers * 2);

    // Start worker pool.
    pool_ = new ThreadPool(num_workers, queue_size);
    pool_->StartWorkers();
  }

  void Receive(Channel *channel, Message *message) override {
    if (output_ == nullptr) {
      // No receiver.
      delete message;
    } else {
      // Send message to output in one of the worker threads.
      pool_->Schedule([this, message]() {
        output_->Send(message);
      });
    }
  }

  void Done(Task *task) override {
    // Stop all worker threads.
    if (pool_ != nullptr) {
      delete pool_;
      pool_ = nullptr;
    }
  }

 private:
  // Thread pool for dispatching messages.
  ThreadPool *pool_ = nullptr;

  // Output channel.
  Channel *output_;
};

REGISTER_TASK_PROCESSOR("workers", Workers);

}  // namespace task
}  // namespace sling


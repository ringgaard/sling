#ifndef TASK_PROCESS_H_
#define TASK_PROCESS_H_

#include "task/task.h"
#include "util/thread.h"

namespace sling {
namespace task {

// A task process runs the task in a separate thread.
class Process : public Processor {
 public:
  // Delete thread.
  ~Process() override { delete thread_; }

  // Start task thread.
  void Start(Task *task) override;

  // Wait for thread to finish.
  void Done(Task *task) override;

  // This method is run in a separate thread when the task is started.
  virtual void Run(Task *task) = 0;

 private:
  // Thread for running task.
  ClosureThread *thread_ = nullptr;
};

}  // namespace task
}  // namespace sling

#endif  // TASK_PROCESS_H_


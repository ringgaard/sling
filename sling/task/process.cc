#include "sling/task/process.h"

#include "sling/util/thread.h"

namespace sling {
namespace task {

void Process::Start(Task *task) {
  // Add task reference to keep task alive while the task thread is running.
  task->AddRef();

  // Execute the Run() method in a new thread.
  thread_ = new ClosureThread([this, task]() {
    // Run task.
    Run(task);

    // Release task reference to allow task to complete.
    task->Release();
  });

  // Start task thread.
  thread_->SetJoinable(true);
  thread_->Start();
}

void Process::Done(Task *task) {
  // Wait for task thread to finish.
  if (thread_ != nullptr) thread_->Join();
}

}  // namespace task
}  // namespace sling


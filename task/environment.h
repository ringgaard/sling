#ifndef TASK_ENVIRONMENT_H_
#define TASK_ENVIRONMENT_H_

#include <atomic>
#include <string>

#include "base/types.h"

namespace sling {
namespace task {

class Channel;
class Task;

// Lock-free counter for statistics.
class Counter {
 public:
  // Increment counter.
  void Increment() { ++value_; }
  void Increment(int64 delta) { value_ += delta; }

  // Reset counter.
  void Reset() { value_ = 0; }

  // Set counter value.
  void Set(int64 value) { value_ = value; }

  // Return counter value.
  int64 value() const { return value_; }

 private:
  std::atomic<int64> value_{0};
};

// Container environment interface.
class Environment {
 public:
  virtual ~Environment() = default;

  // Return statistics counter.
  virtual Counter *GetCounter(const string &name) = 0;

  // Notify that channel has completed.
  virtual void ChannelCompleted(Channel *channel) = 0;

  // Notify that task has completed.
  virtual void TaskCompleted(Task *task) = 0;
};

}  // namespace task
}  // namespace sling

#endif  // TASK_ENVIRONMENT_H_


#ifndef TASK_CONTAINER_H_
#define TASK_CONTAINER_H_

#include <condition_variable>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/flags.h"
#include "base/types.h"
#include "task/environment.h"
#include "task/message.h"
#include "task/task.h"
#include "util/mutex.h"
#include "util/threadpool.h"

DECLARE_int32(event_manager_threads);
DECLARE_int32(event_manager_queue_size);

namespace sling {
namespace task {

// A task container manages a set of tasks with inputs and outputs. These tasks
// are connected by channels which allow the tasks to communicate.
class Container : public Environment {
 public:
  // Initialize container.
  Container();

  // Destroy container.
  ~Container();

  // Create a new resource in the container.
  Resource *CreateResource(const string &filename, const Format &format);

  // Create new resources in the container. If the filename contains wild cards
  // or has a @n specifier, a set of sharded resources are returned.
  std::vector<Resource *> CreateResources(const string &filename,
                                          const Format &format);

  // Create a set of sharded resources.
  std::vector<Resource *> CreateShardedResources(const string &basename,
                                                 int shards,
                                                 const Format &format);

  // Create a new channel in the container.
  Channel *CreateChannel(const Format &format);
  std::vector<Channel *> CreateChannels(const Format &format, int shards);

  // Create a new task in the container.
  Task *CreateTask(const string &type,
                   const string &name,
                   Shard shard = Shard());

  // Create a set of sharded tasks.
  std::vector<Task *> CreateTasks(const string &type,
                                  const string &name,
                                  int shards);

  // Connect producer to consumer with a channel.
  Channel *Connect(const Port &producer,
                   const Port &consumer,
                   const Format &format);
  Channel *Connect(Task *producer,
                   Task *consumer,
                   const string format) {
    return Connect(Port(producer, "output"),
                   Port(consumer, "input"),
                   Format("message", format));
  }

  // Bind resource to input.
  Binding *BindInput(Task *task, Resource *resource,
                     const string &input);

  // Bind resource to output.
  Binding *BindOutput(Task *task, Resource *resource,
                      const string &output);

  // Initialize and start tasks.
  void Run();

  // Wait for all tasks to complete.
  void Wait();

  // Wait for all tasks to complete with timeout. Return false on timeout.
  bool Wait(int ms);

  // Check if all tasks have completed.
  bool Completed();

  // Dump counters to log.
  void DumpCounters();

  // Task environment interface.
  Counter *GetCounter(const string &name) override;
  void ChannelCompleted(Channel *channel) override;
  void TaskCompleted(Task *task) override;

 private:
  // List of tasks in container indexed by id.
  std::vector<Task *> tasks_;

  // List of channels in container indexed by id.
  std::vector<Channel *> channels_;

  // List of resources registered in container.
  std::vector<Resource *> resources_;

  // Statistics counters.
  std::unordered_map<string, Counter *> counters_;

  // Worker queue for event dispatching.
  ThreadPool *event_dispatcher_;

  // Mutex for protecting container state.
  Mutex mu_;

  // Signal that all tasks have completed.
  std::condition_variable completed_;
};

}  // namespace task
}  // namespace sling

#endif  // TASK_CONTAINER_H_


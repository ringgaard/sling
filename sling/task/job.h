#ifndef SLING_TASK_JOB_H_
#define SLING_TASK_JOB_H_

#include <condition_variable>
#include <string>
#include <unordered_map>
#include <vector>

#include "sling/base/flags.h"
#include "sling/base/types.h"
#include "sling/task/environment.h"
#include "sling/task/message.h"
#include "sling/task/task.h"
#include "sling/util/mutex.h"
#include "sling/util/threadpool.h"

DECLARE_int32(event_manager_threads);
DECLARE_int32(event_manager_queue_size);

namespace sling {
namespace task {

// A stage is a set of tasks that can be run concurrently. A stage can have
// dependencies on other stages, which must have completed before this stage
// can run.
class Stage {
 public:
  // Add task to stage.
  void AddTask(Task *task);

  // Add stage dependency.
  void AddDependency(Stage *dependency);

  // Check if stage is ready to run, i.e. all the dependent stages are done.
  bool Ready();

  // Run tasks in stage.
  void Run();

  // Notification that task in stage has completed.
  void TaskCompleted(Task *task) { num_completed_++; }

  // Check if all tasks have completed.
  bool done() const { return num_completed_ == tasks_.size(); }

 private:
  // Tasks in stage.
  std::vector<Task *> tasks_;

  // Other stages that this stage depend on.
  std::vector<Stage *> dependencies_;

  // Number of tasks in stage that have completed.
  int num_completed_ = 0;
};

// A job manages a set of tasks with inputs and outputs. These tasks are
// connected by channels which allow the tasks to communicate.
class Job : public Environment {
 public:
  // Initialize job.
  Job();

  // Destroy job.
  ~Job();

  // Create a new resource for the job.
  Resource *CreateResource(const string &filename, const Format &format);

  // Create new resources for the job. If the filename contains wild cards
  // or has a @n specifier, a set of sharded resources are returned.
  std::vector<Resource *> CreateResources(const string &filename,
                                          const Format &format);

  // Create a set of sharded resources.
  std::vector<Resource *> CreateShardedResources(const string &basename,
                                                 int shards,
                                                 const Format &format);

  // Create a new channel for the job.
  Channel *CreateChannel(const Format &format);
  std::vector<Channel *> CreateChannels(const Format &format, int shards);

  // Create a new task for the job.
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
  bool Done();

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

  // List of stages for running the tasks.
  std::vector<Stage *> stages_;

  // Statistics counters.
  std::unordered_map<string, Counter *> counters_;

  // Worker queue for event dispatching.
  ThreadPool *event_dispatcher_;

  // Mutex for protecting job state.
  Mutex mu_;

  // Signal that all tasks have completed.
  std::condition_variable completed_;
};

}  // namespace task
}  // namespace sling

#endif  // SLING_TASK_JOB_H_


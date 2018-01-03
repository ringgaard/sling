#include "sling/task/job.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "sling/base/flags.h"
#include "sling/base/logging.h"
#include "sling/file/file.h"
#include "sling/string/numbers.h"
#include "sling/string/printf.h"
#include "sling/util/mutex.h"

DEFINE_int32(event_manager_threads, 10,
             "number of threads for job event manager");

DEFINE_int32(event_manager_queue_size, 1024,
             "size of event queue for job");

namespace sling {
namespace task {

void Stage::AddTask(Task *task) {
  tasks_.push_back(task);
}

bool Stage::Ready() {
  for (Stage *dependency : dependencies_) {
    if (!dependency->done()) return false;
  }
  return true;
}

void Stage::Run() {
  for (int i = tasks_.size() - 1; i >= 0; --i) {
    LOG(INFO) << "Start " << tasks_[i]->ToString();
    tasks_[i]->Start();
  }
}

Job::Job() {
  // Start event dispatcher.
  event_dispatcher_ = new ThreadPool(FLAGS_event_manager_threads,
                                     FLAGS_event_manager_queue_size);
  event_dispatcher_->StartWorkers();
}

Job::~Job() {
  delete event_dispatcher_;
  for (auto t : tasks_) delete t;
  for (auto c : channels_) delete c;
  for (auto r : resources_) delete r;
  for (auto c : counters_) delete c.second;
}

Resource *Job::CreateResource(const string &filename,
                              const Format &format) {
  MutexLock lock(&mu_);
  Resource *r = new Resource(resources_.size(), filename, Shard(), format);
  resources_.push_back(r);
  return r;
}

std::vector<Resource *> Job::CreateResources(const string &filename,
                                             const Format &format) {
  std::vector<string> filenames;
  bool sharded = false;
  if (filename.find('?') != -1 || filename.find('*') != -1) {
    // Match file name pattern.
    CHECK(File::Match(filename, &filenames));
    if (filenames.empty()) {
      filenames.push_back(filename);
    } else {
      sharded = true;
    }
  } else {
    // Expand sharded filename (base@nnn).
    size_t at = filename.find('@');
    int shards;
    if (at != -1 && safe_strto32(filename.substr(at + 1), &shards)) {
      string base = filename.substr(0, at);
      for (int shard = 0; shard < shards; ++shard) {
        filenames.push_back(
            StringPrintf("%s-%05d-of-%05d", base.c_str(), shard, shards));
      }
      sharded = true;
    } else {
      // Singleton file resource.
      filenames.push_back(filename);
    }
  }

  // Sort file names.
  std::sort(filenames.begin(), filenames.end());

  // Create resources for files.
  MutexLock lock(&mu_);
  std::vector<Resource *> resources;
  for (int i = 0; i < filenames.size(); ++i) {
    Shard shard = sharded ? Shard(i, filenames.size()) : Shard();
    int id = resources_.size();
    Resource *r = new Resource(id, filenames[i],  shard, format);
    resources_.push_back(r);
    resources.push_back(r);
  }

  return resources;
}

std::vector<Resource *> Job::CreateShardedResources(
    const string &basename,
    int shards,
    const Format &format) {
  // Create resources for files.
  MutexLock lock(&mu_);
  std::vector<Resource *> resources;
  for (int i = 0; i < shards; ++i) {
    Shard shard(i, shards);
    string fn =
        StringPrintf("%s-%05d-of-%05d", basename.c_str(), i, shards);
    int id = resources_.size();
    Resource *r = new Resource(id, fn,  shard, format);
    resources_.push_back(r);
    resources.push_back(r);
  }
  return resources;
}

Channel *Job::CreateChannel(const Format &format) {
  MutexLock lock(&mu_);
  Channel *channel = new Channel(channels_.size(), format);
  channels_.push_back(channel);
  return channel;
}

std::vector<Channel *> Job::CreateChannels(const Format &format, int shards) {
  MutexLock lock(&mu_);
  std::vector<Channel *> channels;
  for (int i = 0; i < shards; ++i) {
    Channel *channel = new Channel(channels_.size(), format);
    channels_.push_back(channel);
    channels.push_back(channel);
  }
  return channels;
}

Task *Job::CreateTask(const string &type,
                      const string &name,
                      Shard shard) {
  MutexLock lock(&mu_);
  Task *task = new Task(this, tasks_.size(), type, name, shard);
  tasks_.push_back(task);
  return task;
}

std::vector<Task *> Job::CreateTasks(const string &type,
                                     const string &name,
                                     int shards) {
  MutexLock lock(&mu_);
  std::vector<Task *> tasks;
  for (int i = 0; i < shards; ++i) {
    Task *task = new Task(this, tasks_.size(), type, name, Shard(i, shards));
    tasks_.push_back(task);
    tasks.push_back(task);
  }
  return tasks;
}

Channel *Job::Connect(const Port &producer,
                      const Port &consumer,
                      const Format &format) {
  Channel *channel = CreateChannel(format);
  channel->ConnectConsumer(consumer);
  channel->ConnectProducer(producer);
  return channel;
}

Binding *Job::BindInput(Task *task,
                        Resource *resource,
                        const string &input) {
  MutexLock lock(&mu_);
  Binding *binding = new Binding(input, resource);
  task->AttachInput(binding);
  return binding;
}

Binding *Job::BindOutput(Task *task,
                         Resource *resource,
                         const string &output) {
  MutexLock lock(&mu_);
  Binding *binding = new Binding(output, resource);
  task->AttachOutput(binding);
  return binding;
}

Counter *Job::GetCounter(const string &name) {
  MutexLock lock(&mu_);
  Counter *&counter = counters_[name];
  if (counter == nullptr) counter = new Counter();
  return counter;
}

void Job::ChannelCompleted(Channel *channel) {
  MutexLock lock(&mu_);
  LOG(INFO) << "Channel " << channel->id() << " completed";

  event_dispatcher_->Schedule([this, channel]() {
    // Notify consumer that one of its input channels has been closed.
    channel->consumer().task()->OnClose(channel);
  });
}

void Job::TaskCompleted(Task *task) {
  LOG(INFO) << "Task " << task->ToString() << " completed";

  event_dispatcher_->Schedule([this, task](){
    // Notify task that it is done.
    task->Done();

    // Notify stage about task completion.
    std::unique_lock<std::mutex> lock(mu_);
    task->stage()->TaskCompleted(task);

    // Start any new stages that are ready to run.
    for (Stage *stage : stages_) {
      if (stage->done()) continue;
      if (stage->Ready()) stage->Run();
    }

    // Check if all stages have completed.
    if (Done()) completed_.notify_all();
  });
}

bool Job::Done() {
  for (Stage *stage : stages_) {
    if (!stage->done()) return false;
  }
  return true;
}

void Job::Run() {
  // Determine producer (if any) of each resource.
  std::vector<Task *> producer(resources_.size());
  for (Task *task : tasks_) {
    for (Binding *binding : task->outputs()) {
      producer[binding->resource()->id()] = task;
    }
  }

  // Sort tasks in dependency order.
  std::vector<bool> ready(tasks_.size());
  std::vector<Task *> order;
  while (order.size() < tasks_.size()) {
    for (Task *task : tasks_) {
      if (ready[task->id()]) continue;

      // Check if all input resources are ready.
      bool ok = true;
      for (Binding *binding : task->inputs()) {
        Task *dependency = producer[binding->resource()->id()];
        if (dependency != nullptr && !ready[dependency->id()]) {
          ok = false;
          break;
        }
      }
      if (!ok) continue;

      // Check if all source channels are ready.
      ok = true;
      for (Channel *channel : task->sources()) {
        Task *dependency = channel->producer().task();
        if (dependency != nullptr && !ready[dependency->id()]) {
          ok = false;
          break;
        }
      }
      if (!ok) continue;

      // Task is ready.
      ready[task->id()] = true;
      order.push_back(task);
    }
  }

  // Create stages.
  for (;;) {
    // Find next task that is not assigned to a stage yet.
    Task *task = nullptr;
    for (Task *t : order) {
      if (t->stage() == nullptr) {
        task = t;
        break;
      }
    }
    if (task == nullptr) break;

    // Create a new stage for the task.
    Stage *stage = new Stage();
    stages_.push_back(stage);

    // Assign the transitive closure over source and sink channels to stage.
    std::vector<Task *> queue;
    queue.push_back(task);
    while (!queue.empty()) {
      Task *t = queue.back();
      queue.pop_back();
      if (t->stage() != nullptr) continue;
      t->set_stage(stage);
      for (Channel *channel : t->sources()) {
        queue.push_back(channel->producer().task());
      }
      for (Channel *channel : t->sinks()) {
        queue.push_back(channel->consumer().task());
      }
    }
  }

  // Assign tasks to stages.
  for (Task *task : order) {
    task->stage()->AddTask(task);
  }

  // Initialize all tasks.
  for (Task *task : order) {
    LOG(INFO) << "Initialize " << task->ToString();
    task->Init();
  }

  // Start all stages that are ready.
  for (Stage *stage : stages_) {
    if (stage->Ready()) stage->Run();
  }

  LOG(INFO) << "All systems GO";
}

void Job::Wait() {
  std::unique_lock<std::mutex> lock(mu_);
  while (!Done()) completed_.wait(lock);
}

bool Job::Wait(int ms) {
  auto timeout = std::chrono::milliseconds(ms);
  auto expire = std::chrono::system_clock::now() + timeout;
  std::unique_lock<std::mutex> lock(mu_);
  while (!Done()) {
    if (completed_.wait_until(lock, expire) == std::cv_status::timeout) {
      return false;
    }
  }
  return true;
}

void Job::DumpCounters() {
  MutexLock lock(&mu_);
  std::vector<std::pair<string, Counter *>> stats(counters_.begin(),
                                                  counters_.end());
  std::sort(stats.begin(), stats.end());
  for (auto &s : stats) {
    LOG(INFO) << s.first << " = " << s.second->value();
  }
}

}  // namespace task
}  // namespace sling


#include "sling/task/container.h"

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
             "number of threads for task container event manager");

DEFINE_int32(event_manager_queue_size, 1024,
             "size of event queue for task container");

namespace sling {
namespace task {

Container::Container() {
  // Start event dispatcher.
  event_dispatcher_ = new ThreadPool(FLAGS_event_manager_threads,
                                     FLAGS_event_manager_queue_size);
  event_dispatcher_->StartWorkers();
}

Container::~Container() {
  delete event_dispatcher_;
  for (auto t : tasks_) delete t;
  for (auto c : channels_) delete c;
  for (auto r : resources_) delete r;
  for (auto c : counters_) delete c.second;
}

Resource *Container::CreateResource(const string &filename,
                                    const Format &format) {
  MutexLock lock(&mu_);
  return new Resource(filename, Shard(), format);
}

std::vector<Resource *> Container::CreateResources(const string &filename,
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
    Resource *r = new Resource(filenames[i],  shard, format);
    resources_.push_back(r);
    resources.push_back(r);
  }

  return resources;
}

std::vector<Resource *> Container::CreateShardedResources(
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
    Resource *r = new Resource(fn,  shard, format);
    resources_.push_back(r);
    resources.push_back(r);
  }
  return resources;
}

Channel *Container::CreateChannel(const Format &format) {
  MutexLock lock(&mu_);
  Channel *channel = new Channel(channels_.size(), format);
  channels_.push_back(channel);
  return channel;
}

std::vector<Channel *> Container::CreateChannels(
    const Format &format, int shards) {
  MutexLock lock(&mu_);
  std::vector<Channel *> channels;
  for (int i = 0; i < shards; ++i) {
    Channel *channel = new Channel(channels_.size(), format);
    channels_.push_back(channel);
    channels.push_back(channel);
  }
  return channels;
}

Task *Container::CreateTask(const string &type,
                            const string &name,
                            Shard shard) {
  MutexLock lock(&mu_);
  Task *task = new Task(this, tasks_.size(), type, name, shard);
  tasks_.push_back(task);
  return task;
}

std::vector<Task *> Container::CreateTasks(const string &type,
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

Channel *Container::Connect(const Port &producer,
                            const Port &consumer,
                            const Format &format) {
  Channel *channel = CreateChannel(format);
  channel->ConnectConsumer(consumer);
  channel->ConnectProducer(producer);
  return channel;
}

Binding *Container::BindInput(Task *task,
                              Resource *resource,
                              const string &input) {
  MutexLock lock(&mu_);
  Binding *binding = new Binding(input, resource);
  task->AttachInput(binding);
  return binding;
}

Binding *Container::BindOutput(Task *task,
                               Resource *resource,
                               const string &output) {
  MutexLock lock(&mu_);
  Binding *binding = new Binding(output, resource);
  task->AttachOutput(binding);
  return binding;
}

Counter *Container::GetCounter(const string &name) {
  MutexLock lock(&mu_);
  Counter *&counter = counters_[name];
  if (counter == nullptr) counter = new Counter();
  return counter;
}

void Container::ChannelCompleted(Channel *channel) {
  MutexLock lock(&mu_);
  LOG(INFO) << "Channel " << channel->id() << " completed";

  event_dispatcher_->Schedule([this, channel]() {
    // Notify consumer that one of its input channels has been closed.
    channel->consumer().task()->OnClose(channel);
  });
}

void Container::TaskCompleted(Task *task) {
  LOG(INFO) << "Task " << task->ToString() << " completed";

  event_dispatcher_->Schedule([this, task](){
    // Notify task that it is done.
    task->Done();

    // Check if all tasks have completed.
    std::unique_lock<std::mutex> lock(mu_);
    if (Completed()) completed_.notify_all();
  });
}

bool Container::Completed() {
  for (Task *task : tasks_) {
    if (!task->completed()) return false;
  }
  return true;
}

void Container::Run() {
  // Initialize all tasks.
  for (Task *task : tasks_) {
    LOG(INFO) << "Initialize " << task->ToString();
    task->Init();
  }

  // Start all tasks.
  // Start in reverse to "ensure" that dependent tasks are ready to receive.
  // TODO: sort tasks in dependency order.
  for (int i = tasks_.size() - 1; i >= 0; --i) {
    LOG(INFO) << "Start " << tasks_[i]->ToString();
    tasks_[i]->Start();
  }

  LOG(INFO) << "All systems GO";
}

void Container::Wait() {
  std::unique_lock<std::mutex> lock(mu_);
  while (!Completed()) completed_.wait(lock);
}

bool Container::Wait(int ms) {
  auto timeout = std::chrono::milliseconds(ms);
  auto expire = std::chrono::system_clock::now() + timeout;
  std::unique_lock<std::mutex> lock(mu_);
  while (!Completed()) {
    if (completed_.wait_until(lock, expire) == std::cv_status::timeout) {
      return false;
    }
  }
  return true;
}

void Container::DumpCounters() {
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


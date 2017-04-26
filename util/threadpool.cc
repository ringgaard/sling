#include "base/logging.h"
#include "util/threadpool.h"

namespace sling {

ThreadPool::ThreadPool(int num_workers, int queue_size)
    : num_workers_(num_workers), queue_size_(queue_size) {}

ThreadPool::~ThreadPool() {
  // Wait until all tasks have been completed.
  Shutdown();

  // Wait until all workers have terminated.
  for (auto &t : workers_) t.Join();
}

void ThreadPool::StartWorkers() {
  // Create worker threads.
  CHECK(workers_.empty());
  for (int i = 0; i < num_workers_; ++i) {
    workers_.emplace_back([this]() {
      // Keep processing tasks until done.
      Task task;
      while (FetchTask(&task)) task();
    });
  }

  // Start worker threads.
  for (auto &t : workers_) {
    t.SetJoinable(true);
    t.Start();
  }
}

void ThreadPool::Schedule(Task &&task) {
  std::unique_lock<std::mutex> lock(mu_);
  while (tasks_.size() >= queue_size_) {
    nonfull_.wait(lock);
  }
  tasks_.push(std::move(task));
  nonempty_.notify_one();
}

bool ThreadPool::FetchTask(Task *task) {
  std::unique_lock<std::mutex> lock(mu_);
  while (tasks_.empty()) {
    if (done_) return false;
    nonempty_.wait(lock);
  }
  *task = tasks_.front();
  tasks_.pop();
  nonfull_.notify_one();
  return true;
}

void ThreadPool::Shutdown() {
  // Notify all threads that we are done.
  std::lock_guard<std::mutex> lock(mu_);
  done_ = true;
  nonempty_.notify_all();
}

}  // namespace sling


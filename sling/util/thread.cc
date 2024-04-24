// Copyright 2013 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sling/util/thread.h"

#include "sling/base/logging.h"

namespace sling {

void *Thread::ThreadMain(void *arg) {
  Thread *thread = static_cast<Thread *>(arg);
  thread->Run();
  return nullptr;
}

Thread::Thread() : running_(false) {}
Thread::~Thread() {}

void Thread::Start() {
  CHECK(!running_);
  pthread_create(&thread_, nullptr, &ThreadMain, this);
  running_ = true;

  // Detach the thread if it is not joinable.
  if (!joinable_) {
    pthread_detach(thread_);
  }
}

void Thread::Join() {
  if (!running_) return;
  if (joinable_) {
    running_ = false;
    void *unused;
    pthread_join(thread_, &unused);
  } else {
    LOG(WARNING) << "Thread " << thread_ << " is not joinable";
  }
}

void Thread::SetJoinable(bool joinable) {
  CHECK(!running_) << "Can't SetJoinable() on a running thread";
  joinable_ = joinable;
}

bool Thread::IsSelf() const {
  return pthread_equal(thread_, pthread_self());
}

void ClosureThread::Run() {
  // Run closure.
  closure_();
}

void TimerThread::Start(int interval) {
  interval_ = interval;
  SetJoinable(true);
  Thread::Start();
}

void TimerThread::Stop() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    done_ = true;
    stop_.notify_one();
  }
  Join();
}

void TimerThread::Run() {
  std::unique_lock<std::mutex> lock(mu_);
  for (;;) {
    stop_.wait_for(lock, std::chrono::milliseconds(interval_));
    if (done_) break;
    closure_();
  }
}

WorkerPool::~WorkerPool() {
  for (auto *t : workers_) delete t;
}

void WorkerPool::Start(int num_workers, const Worker &worker) {
  // Create worker threads.
  int first = workers_.size();
  for (int i = 0; i < num_workers; ++i) {
    int index = first + i;
    ClosureThread *t = new ClosureThread([worker, index]() { worker(index); });
    workers_.push_back(t);
  }

  // Start worker threads.
  for (int i = first; i < workers_.size(); ++i) {
    workers_[i]->SetJoinable(true);
    workers_[i]->Start();
  }
}

void WorkerPool::Join() {
  for (auto *t : workers_) t->Join();
}

}  // namespace sling


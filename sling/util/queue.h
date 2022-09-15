// Copyright 2022 Ringgaard Research ApS
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

#ifndef SLING_UTIL_QUEUE_H_
#define SLING_UTIL_QUEUE_H_

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>

namespace sling {

// Message queue for producer/consumer threads.
template<typename T> class Queue {
 public:
  // Add message to queue.
  void put(T msg) {
    std::unique_lock<std::mutex> lock(mu_);
    queue_.push(msg);
    signal_.notify_one();
  }

  // Get element from queue with optional timeout in milliseconds.
  T get(int timeout = -1) {
    std::unique_lock<std::mutex> lock(mu_);
    while (queue_.empty()) {
      if (timeout == -1) {
        signal_.wait(lock);
      } else {
        auto st = signal_.wait_for(lock, std::chrono::milliseconds(timeout));
        if (st == std::cv_status::timeout) return T();
      }
    }
    T msg = queue_.front();
    queue_.pop();
    return msg;
  }

 private:
  // Message queue.
  std::queue<T> queue_;

  // Mutex for serializing access to task queue.
  std::mutex mu_;

  // Signal to notify about new tasks in queue.
  std::condition_variable signal_;
};

}  // namespace sling

#endif  // SLING_UTIL_QUEUE_H_


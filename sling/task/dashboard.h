// Copyright 2017 Google Inc.
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

#ifndef SLING_TASK_DASHBOARD_H_
#define SLING_TASK_DASHBOARD_H_

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sling/base/clock.h"
#include "sling/base/types.h"
#include "sling/net/http-server.h"
#include "sling/net/static-content.h"
#include "sling/task/job.h"
#include "sling/util/json.h"
#include "sling/util/mutex.h"
#include "sling/util/thread.h"

namespace sling {
namespace task {

// Monitor for collecting performance data.
class PerformanceMonitor {
 public:
  // Start monitor collecting data every 'interval' ms.
  PerformanceMonitor(int interval);

  // Stop monitor.
  ~PerformanceMonitor();

  // Performance sample.
  struct Sample {
    time_t time;     // sample time
    int64 cpu;       // cpu usage (percent)
    int64 ram;       // memory usage (bytes)
    int64 io;        // io activity (bytes/sec)
    int64 temp;      // temperature (celsius)
    int64 read;      // disk read (bytes/sec)
    int64 write;     // disk written (bytes/sec)
    int64 receive;   // network received (bytes/sec)
    int64 transmit;  // network transmitted (bytes/sec)
  };

  // Return sampled performace records.
  std::vector<Sample> samples() {
    MutexLock lock(&mu_);
    return samples_;
  }

  // Collect performance sample.
  void Collect();

 private:
  // Collected performance data.
  std::vector<Sample> samples_;

  // Last sampled I/O values.
  int64 last_rd_;
  int64 last_wr_;
  int64 last_rx_;
  int64 last_tx_;

  // High-precision clock.
  Clock clock_;

  // Timer for collecting performance data.
  TimerThread timer_{[&]() { Collect(); }};

  // Mutex for serializing access to performance data.
  Mutex mu_;
};

// Dashboard for monitoring jobs.
class Dashboard : public Monitor {
 public:
  enum Status {
    IDLE,        // dashboard is idle when jobs are not being monitored
    MONITORED,   // job status has been requested by client
    FINAL,       // all jobs has completed
    SYNCHED,     // final status has been sent to client
    TERMINAL,    // dashboard ready for shutdown
  };

  // List of counters.
  typedef std::vector<std::pair<string, int64>> CounterList;

  Dashboard();
  ~Dashboard();

  // Register job status service.
  void Register(HTTPServer *http);

  // Wait until final status has been reported or timeout (in seconds).
  void Finalize(int timeout);

  // Get job status in JSON format.
  void GetStatus(JSON::Object *json);
  void GetStatus(IOBuffer *output);

  // Handle job status queries.
  void HandleStatus(HTTPRequest *request, HTTPResponse *response);

  // Job monitor interface.
  void OnJobStart(Job *job) override;
  void OnJobDone(Job *job) override;

 private:
  // Status for active or completed job.
  struct JobStatus {
    JobStatus(Job *job) : job(job) {}
    Job *job;               // job object or null if job has completed
    string name;            // job name
    int64 started = 0;      // job start time
    int64 ended = 0;        // job completion time
    CounterList counters;   // final job counters
  };

  // List of active and completed jobs.
  std::vector<JobStatus *> jobs_;

  // Map of active jobs.
  std::unordered_map<Job *, JobStatus *> active_jobs_;

  // Dashboard monitoring status. This is used for the final handshake to
  // report the final status of the jobs before termination.
  Status status_ = IDLE;

  // Dashboard app.
  StaticContent common_{"/common", "app"};
  StaticContent app_{"/", "sling/task/app"};

  // Start time.
  int64 start_time_;

  // Completion time.
  int64 end_time_;

  // Performance monitor.
  PerformanceMonitor perfmon_{20000};

  // Mutex for serializing access to dashboard.
  Mutex mu_;
};

}  // namespace task
}  // namespace sling

#endif  // SLING_TASK_DASHBOARD_H_


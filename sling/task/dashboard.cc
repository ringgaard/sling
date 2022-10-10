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

#include "sling/task/dashboard.h"

#include <time.h>
#include <unistd.h>
#include <sstream>

#include "sling/base/perf.h"
#include "sling/util/json.h"

namespace sling {
namespace task {

Dashboard::Dashboard() {
  start_time_ = time(0);
}

Dashboard::~Dashboard() {
  for (auto *j : jobs_) delete j;
}

void Dashboard::Register(HTTPServer *http) {
  http->Register("/status", this, &Dashboard::HandleStatus);
  common_.Register(http);
  app_.Register(http);
}

void Dashboard::GetStatus(IOBuffer *output) {
  MutexLock lock(&mu_);

  // Output current time and status.
  bool running = (status_ < FINAL);
  JSON::Object json;
  json.Add("time", running ? time(0) : end_time_);
  json.Add("started", start_time_);
  json.Add("finished", running ? 0 : 1);

  // Output jobs.
  JSON::Array *jobs = json.AddArray("jobs");
  for (JobStatus *status : jobs_) {
    JSON::Object *job = jobs->AddObject();
    job->Add("name", status->name);
    job->Add("started", status->started);
    if (status->ended != 0) job->Add("ended", status->ended);

    if (status->job != nullptr) {
      // Output stages for running job.
      JSON::Array *stages = job->AddArray("stages");
      for (Stage *stage : status->job->stages()) {
        JSON::Object *st = stages->AddObject();
        st->Add("tasks", stage->num_tasks());
        st->Add("done", stage->num_completed_tasks());
      }

      // Output counters for job.
      JSON::Object *counters = job->AddObject("counters");
      status->job->IterateCounters(
        [counters](const string &name, Counter *counter) {
          counters->Add(name, counter->value());
        }
      );
    } else {
      // Output counters for job.
      JSON::Object *counters = json.AddObject("counters");
      for (const auto &c : status->counters) {
        counters->Add(c.first, c.second);
      }
    }
  }

  // Output resource usage.
  Perf perf;
  perf.Sample();

  json.Add("utime", perf.utime());
  json.Add("stime", perf.stime());
  json.Add("mem", running ? perf.memory() : Perf::peak_memory_usage());
  json.Add("ioread", perf.ioread());
  json.Add("iowrite", perf.iowrite());
  json.Add("filerd", Perf::file_read());
  json.Add("filewr", Perf::file_write());
  json.Add("netrx", Perf::network_receive());
  json.Add("nettx", Perf::network_transmit());
  json.Add("flops", perf.flops());
  json.Add("temperature",
           running ? perf.cputemp() : Perf::peak_cpu_temperature());

  json.Write(output);
}

void Dashboard::HandleStatus(HTTPRequest *request, HTTPResponse *response) {
  response->set_content_type("text/json; charset=utf-8");
  GetStatus(response->buffer());
  if (status_ == IDLE) status_ = MONITORED;
  if (status_ == FINAL) status_ = SYNCHED;
}

void Dashboard::OnJobStart(Job *job) {
  MutexLock lock(&mu_);

  // Add job to job list.
  JobStatus *status = new JobStatus(job);
  jobs_.push_back(status);
  status->name = job->name();
  status->started = time(0);
  active_jobs_[job] = status;
}

void Dashboard::OnJobDone(Job *job) {
  MutexLock lock(&mu_);

  // Get job status.
  JobStatus *status = active_jobs_[job];
  CHECK(status != nullptr);

  // Record job completion time.
  status->ended = time(0);

  // Update final counter values.
  job->IterateCounters([status](const string &name, Counter *counter) {
    status->counters.emplace_back(name, counter->value());
  });

  // Remove job from active job list.
  active_jobs_.erase(job);
  status->job = nullptr;
}

void Dashboard::Finalize(int timeout) {
  if (status_ == MONITORED) {
    // Signal that all jobs are done.
    end_time_ = time(0);
    status_ = FINAL;

    // Wait until final status has been sent back.
    for (int wait = 0; wait < timeout && status_ != SYNCHED; ++wait) sleep(1);
  }
  status_ = TERMINAL;
}

}  // namespace task
}  // namespace sling


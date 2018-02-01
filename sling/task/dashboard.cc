#include "sling/task/dashboard.h"

#include <time.h>
#include <sstream>

namespace sling {
namespace task {

Dashboard::~Dashboard() {
  for (auto *j : jobs_) delete j;
}

void Dashboard::Register(HTTPServer *http) {
  http->Register("/status", this, &Dashboard::HandleStatus);
}

void Dashboard::HandleStatus(HTTPRequest *request, HTTPResponse *response) {
  MutexLock lock(&mu_);

  // Build job status reply in JSON format.
  std::stringstream out;
  out << "{\"time\":" << time(0);

  out << ",\"jobs\":[";
  bool first_job = true;
  for (JobStatus *status : jobs_) {
    if (!first_job) out << ",";
    first_job = false;
    out << "{\"name\":\"" << status->name << "\"";
    out << ",\"started\":" << status->started;
    if (status->ended != 0) out << ",\"ended\":" << status->ended;

    if (status->job != nullptr) {
      // Output stages for running job.
      out << ",\"stages\":[";
      bool first_stage = true;
      for (Stage *stage : status->job->stages()) {
        if (!first_stage) out << ",";
        first_stage = false;
        out << "{\"tasks\":" << stage->num_tasks()
            << ",\"done\":" << stage->num_completed_tasks() << "}";
      }
      out << "],";

      // Output counters for running job.
      out << "\"counters\":{";
      bool first_counter = true;
      status->job->IterateCounters(
        [&out, &first_counter](const string &name, Counter *counter) {
          if (!first_counter) out << ",";
          first_counter = false;
          out << "\"" << name << "\":" << counter->value();
        }
      );
      out << "}";
    } else {
      // Output counters for completed job.
      out << ",\"counters\":{";
      bool first_counter = true;
      for (auto &counter : status->counters) {
        if (!first_counter) out << ",";
        first_counter = false;
        out << "\"" << counter.first << "\":" << counter.second;
      }
      out << "}";
    }
    out << "}";
  }
  out << "]";
  out << "}";

  // Return reply.
  response->SetContentType("text/json; charset=utf-8");
  response->Append(out.str());
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

}  // namespace task
}  // namespace sling


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

#include <string>

#include "sling/base/logging.h"
#include "sling/db/dbclient.h"
#include "sling/task/task.h"
#include "sling/util/mutex.h"

namespace sling {
namespace task {

// Write incoming messages to database.
class DatabaseWriter : public Processor {
 public:
  ~DatabaseWriter() override {
    for (Message *message : queue_) delete message;
    CHECK(db_.Close());
  }

  void Init(Task *task) override {
    // Get output file.
    Binding *output = task->GetOutput("output");
    if (output == nullptr) {
      LOG(ERROR) << "Output missing";
      return;
    }

    // Get parameters.
    string dbname = output->resource()->name();
    task->Fetch("db_write_batch", &batch_size_);
    int mode = mode_;
    task->Fetch("db_write_mode", &mode);
    mode_ = static_cast<DBMode>(mode);

    // Open database connection.
    Status st = db_.Connect(dbname);
    if (!st.ok()) {
      LOG(FATAL) << "Error connecting to database " << dbname << ": " << st;
    }

    // Switch database to bulk mode to avoid excessive checkpointing during
    // loading of large datasets.
    CHECK(db_.Bulk(true));
  }

  void Receive(Channel *channel, Message *message) override {
    queue_mu_.Lock();

    // Add message to queue.
    queue_.push_back(message);

    // Write batch when queue has a full batch.
    if (queue_.size() >= batch_size_) {
      // Move messages out of queue and release queue lock.
      std::vector<Message *> batch;
      queue_.swap(batch);
      queue_mu_.Unlock();

      // Write batch to database.
      WriteBatch(&batch);
    } else {
      queue_mu_.Unlock();
    }
  }

  void Done(Task *task) override {
    MutexLock lock(&queue_mu_);

    // Flush remaining messages in queue.
    if (!queue_.empty()) WriteBatch(&queue_);

    // Clear bulk mode.
    CHECK(db_.Bulk(false));

    // Close database connection.
    CHECK(db_.Close());
  }

  void WriteBatch(std::vector<Message *> *batch) {
    MutexLock lock(&db_mu_);

    // Prepare batch of record updates.
    std::vector<DBRecord> recs(batch->size());
    for (int i = 0; i < batch->size(); ++i) {
      Message *message = (*batch)[i];
      recs[i].key = message->key();
      recs[i].version = message->serial();
      recs[i].value = message->value();
    }

    // Write records to database.
    Status st = db_.Put(&recs, mode_);
    if (!st.ok()) LOG(FATAL) << "Error writing to database: " << st;

    // Clear batch.
    for (Message *message : *batch) delete message;
    batch->clear();
  }

 private:
  // Database connection for writing records.
  DBClient db_;

  // Update mode for records written to database.
  DBMode mode_ = DBOVERWRITE;

  // Number of records to write in one batch.
  int batch_size_ = 100;

  // Current queue of messages that have not been written to database.
  std::vector<Message *> queue_;

  // Mutex for queue and database writer.
  Mutex queue_mu_;
  Mutex db_mu_;
};

REGISTER_TASK_PROCESSOR("database-writer", DatabaseWriter);

}  // namespace task
}  // namespace sling


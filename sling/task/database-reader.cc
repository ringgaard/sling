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
#include <vector>

#include "sling/base/logging.h"
#include "sling/db/dbclient.h"
#include "sling/task/process.h"
#include "sling/task/task.h"

namespace sling {
namespace task {

// Read records from database and output to channel.
class DatabaseReader : public Process {
 public:
  // Process database records.
  void Run(Task *task) override {
    // Get input database(s).
    auto inputs = task->GetInputs("input");

    // Get output channel.
    Channel *output = task->GetSink("output");
    if (output == nullptr) {
      LOG(ERROR) << "No output channel";
      return;
    }

    // Statistics counters.
    Counter *db_records_read = task->GetCounter("db_records_read");
    Counter *db_bytes_read = task->GetCounter("db_bytes_read");

    for (Binding *input : inputs) {
      // Connect to database.
      DBClient db;
      string dbname = input->resource()->name();
      Status st = db.Connect(dbname, task->name());
      if (!st.ok()) {
        LOG(FATAL) << "Error connecting to database " << dbname << ": " << st;
      }
      uint64 serial = input->resource()->serial();

      // Read records from database and output to output channel.
      DBIterator iterator;
      iterator.batch = task->Get("db_read_batch", 128);
      std::vector<DBRecord> records;
      for (;;) {
        // Read next batch.
        st = db.Next(&iterator, &records);
        if (!st.ok()) {
          if (st.code() == ENOENT) break;
          LOG(FATAL) << "Error reading from database " << dbname << ": " << st;
        }

        // Send messages on output channel.
        for (DBRecord &rec : records) {
          // Update stats.
          db_records_read->Increment();
          db_bytes_read->Increment(rec.key.size() + rec.value.size());

          // Send message with record to output channel.
          Message *message = new Message(rec.key,
                                         serial ? serial : rec.version,
                                         rec.value);
          output->Send(message);
        }
      }

      // Close database connection.
      CHECK(db.Close());
    }

    // Close output channel.
    output->Close();
  }
};

REGISTER_TASK_PROCESSOR("database-reader", DatabaseReader);

}  // namespace task
}  // namespace sling


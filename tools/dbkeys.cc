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

#include <iostream>
#include <string>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/db/dbclient.h"
#include "sling/file/file.h"
#include "sling/file/posix.h"
#include "sling/file/textmap.h"

DEFINE_string(db, "", "Database");
DEFINE_string(chkpt, "", "Checkpoint for resuming");
DEFINE_string(output, "", "Output file for keys");
DEFINE_int32(batch, 8, "Batch size for fetching keys");
DEFINE_int32(maxkeys, 0, "Maximum number of keys to fetch");
DEFINE_int32(progress, 1000, "Report progress for every nth key");

using namespace sling;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Read checkpoint.
  uint64 iterator = 0;
  if (!FLAGS_chkpt.empty()) {
    string data;
    CHECK(File::ReadContents(FLAGS_chkpt, &data));
    iterator = std::stoll(data);
  }

  // Append keys to output file.
  File *f;
  if (FLAGS_output.empty()) {
    f = NewStdoutFile();
  } else {
    f = File::OpenOrDie(FLAGS_output, "a");
  }
  TextMapOutput output(f);

  // Read keys.
  DBClient db;
  CHECK(db.Connect(FLAGS_db, "dbkeys"));
  std::vector<DBRecord> records;
  int num_keys = 0;
  for (;;) {
    // Read next batch.
    Status st = db.Next(&iterator, FLAGS_batch, -1, false, &records);
    if (!st.ok()) {
      if (st.code() == ENOENT) break;
      LOG(FATAL) << "Error reading from database: " << st;
    }

    // Output keys and timestamps.
    for (DBRecord &rec : records) {
      // Skip redirect records.
      if (rec.version == 0) continue;

      // Skip bad keys with control characters.
      bool bad = false;
      const unsigned char *key =
          reinterpret_cast<const unsigned char *>(rec.key.data());
      for (int i = 0; i < rec.key.size(); ++i) {
        if (key[i] < ' ') bad = true;
      }
      if (bad) continue;

      // Stop when maximum reached.
      num_keys++;
      if (FLAGS_maxkeys > 0 && num_keys == FLAGS_maxkeys) break;
      if (FLAGS_progress > 0 && num_keys % FLAGS_progress == 0) {
        std::cerr << num_keys << " keys\r";
      }

      // Output key and timestamp.
      output.Write(rec.key, rec.version);
    }
    if (FLAGS_maxkeys > 0 && num_keys == FLAGS_maxkeys) break;
  }
  if (FLAGS_progress > 0) {
    std::cerr << num_keys << " keys\n";
  }
  CHECK(db.Close());
  output.Close();

  // Write checkpoint.
  if (!FLAGS_chkpt.empty()) {
    CHECK(File::WriteContents(FLAGS_chkpt, std::to_string(iterator)));
  }

  return 0;
}


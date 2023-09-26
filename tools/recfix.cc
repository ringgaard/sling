// Copyright 2023 Ringgaard Research ApS
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

// Examine and fix corrupt record files.

#include <iostream>
#include <string>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/file/recordio.h"

DEFINE_string(file, "", "Record file to check for corruption");
DEFINE_bool(truncate, false, "Truncate record file at point of corruption");
DEFINE_bool(lastchunk, false, "Only examine last chunk");
DEFINE_int32(progress, 0, "Report progress for every nth record");

using namespace sling;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);
  if (FLAGS_file.empty()) {
    Flag::PrintHelp();
    return 1;
  }

  LOG(INFO) << "Checking integrity of " << FLAGS_file;
  RecordReader reader(FLAGS_file);
  uint64 size = reader.size();
  uint64 chunk_size = reader.info().chunk_size;
  LOG(INFO) << "File size: " << size;
  LOG(INFO) << "Chunk size: " << chunk_size;

  // Go to last chunk if requested and supported.
  if (FLAGS_lastchunk && chunk_size != 0) {
    uint64 lastchunk = (size / chunk_size) * chunk_size;
    LOG(INFO) << "Starting at position " << lastchunk;
    CHECK(reader.Seek(lastchunk));
  }

  // Read until error or end of file.
  uint64 position = 0;
  bool corrupt = false;
  int n = 0;
  while (!reader.Done()) {
    // Read next record.
    position = reader.Tell();
    if (FLAGS_progress > 0) {
      if (++n % FLAGS_progress == 0) std::cerr << position << "\r";
    }
    Record record;
    Status st = reader.Read(&record);
    if (!st) {
      LOG(ERROR) << "Error reading record at position " << position;
      corrupt = true;
      break;
    }
  }
  CHECK(reader.Close());
  if (FLAGS_progress > 0) {
    std::cerr << position << "\n";
  }

  // Truncate file at first corrupted record if requested.
  if (corrupt) {
    LOG(ERROR) << "Corrupt record at position " << position << ", "
               << (reader.size() - position) << " bytes lost";

    if (FLAGS_truncate) {
      File *file = File::OpenOrDie(FLAGS_file, "r+");
      Status st = file->Resize(position);
      if (!st) {
        LOG(ERROR) << "Error truncating file";
      } else {
        LOG(INFO) << "File truncated at position " << position;
      }
      CHECK(file->Close());
    }
  } else {
    LOG(INFO) << "No errors found in " << FLAGS_file;
  }

  return 0;
}


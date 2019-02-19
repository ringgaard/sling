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

#ifndef SLING_DB_DB_H_
#define SLING_DB_DB_H_

#include <string>

#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/file/file.h"
#include "sling/file/recordio.h"

namespace sling {

class Database {
 public:
  Database(const string &idxfile,
           const string &recfile,
           const RecordFileOptions &options);
  ~Database();

 private:
  // Magic number and version for identifying database index file.
  static const uint32 MAGIC = 0x46584449;  // IDXF
  static const uint32 VERSION = 1;

  // Index file header.
  struct IndexFileHeader {
    uint32 magic;     // magic number for identifying database index file
    uint32 version;   // index file format version
    uint64 datasize;  // size of record file when index was created/updated
    uint64 numkeys;   // number of keys in index (for truncation check)
  };

  // Database record header.
  struct RecordHeader {
    int64 timestamp;    // time when record was created
    uint64 replaces;    // position of record that this replaces (or 0 for new)
  };

  // Read record at position from database. This separates out the record header
  // from the record value.
  Status ReadRecord(uint64 position, Record *record, const RecordHeader **hdr);

  // Initialize database index.
  void InitializeIndex();

  // Record reader and writer.
  RecordReader *reader_;
  RecordWriter *writer_;

  // Filename for database index.
  string idxfile_;

  // Index of all active records in database.
  RecordFile::Index index_;
};

}  // namespace sling

#endif  // SLING_DB_DB_H_


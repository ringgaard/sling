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

#include "sling/db/db.h"

namespace sling {

Database::Database(const string &idxfile,
                   const string &recfile,
                   const RecordFileOptions &options) : idxfile_(idxfile) {
  // Open data record file for reading and appending. The reader and writer
  // share the same underlying file.
  File *file;
  CHECK(File::Open(recfile, "a+", &file));
  reader_ = new RecordReader(file, options, false);
  writer_ = new RecordWriter(reader_, options);
}

Database::~Database() {
  // Close data record file.
  delete reader_;
  delete writer_;
}

Status Database::ReadRecord(uint64 position,
                            Record *record,
                            const RecordHeader **hdr) {
  // Read record at position.
  Status st = reader_->Seek(position);
  if (!st) return st;
  st = reader_->Read(record);
  if (!st) return st;

  // Get record header.
  CHECK_GE(record->value.size(), sizeof(RecordHeader));
  *hdr = reinterpret_cast<const RecordHeader *>(record->value.data());
  record->value.remove_prefix(sizeof(RecordHeader));

  return Status::OK;
}

void Database::InitializeIndex() {
  // Read index from idx file
  // If idx file is missing, reconstruct it from data file

  // If data file size in index header is less than the record file size,
  // update the remaining records from the last position in the index.
}

}  // namespace sling


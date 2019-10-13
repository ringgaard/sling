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
                   const std::vector<string> &recfiles,
                   const RecordFileOptions &options) : idxfile_(idxfile) {
  // Open data record files for reading and appending. The reader and writer
  // for each data shard share the same underlying file.
  CHECK(!recfiles.empty());
  shards_.resize(recfiles.size());
  datasize_ = 0;
  for (int i = 0; i < recfiles.size(); ++i) {
    Shard &shard = shards_[i];
    File *file;
    CHECK(File::Open(recfiles[i], "a+", &file));
    shard.reader = new RecordReader(file, options, false);
    shard.writer = new RecordWriter(shard.reader, options);
    datasize_ += shard.reader->size();
  }
}

Database::~Database() {
  // Close data record files.
  for (Shard &shard : shards_) {
    delete shard.reader;
    delete shard.writer;
  }
}

void Database::InitializeIndex() {
  // Read index from idx file
  // If idx file is missing, reconstruct it from data file

  // If data file size in index header is less than the record file size,
  // update the remaining records from the last position in the index.
}

}  // namespace sling


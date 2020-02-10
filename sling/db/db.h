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
#include <vector>

#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/file/file.h"
#include "sling/file/recordio.h"

namespace sling {

// Database index. The index is implemented as a file-backed hash table with
// linear probing. The index allows multiple keys with the same value.
class DatabaseIndex {
 public:
   // Invalid index position.
   const static uint64 NPOS = -1;

   // Invalid value.
   const static uint64 NVAL = -1;

   // Open existing index file.
   DatabaseIndex(const string &filename);

   // Create new index file.
   DatabaseIndex(const string &filename, int64 capacity);

   // Flush and close database index.
   ~DatabaseIndex();

   // Flush changes to disk.
   Status Flush(uint64 epoch);

   // Add new entry to index. Returns position of new entry.
   uint64 Add(uint64 key, uint64 value);

   // Look up value in index. Search for first value if pos is not specified.
   // Otherwise, search for the next value after position. Return the (next)
   // value for the key and update position or NVAL if no match is found.
   uint64 Get(uint64 key, uint64 *pos);
   uint64 Get(uint64 key) {
     uint64 pos = NPOS;
     return Get(key, &pos);
   }

   // Update value of existing entry. Returns position of updated entry or NPOS
   // if the entry was not found.
   uint64 Update(uint64 key, uint64 value, uint64 pos = NPOS);

   // Delete entry in index. Return the value for the deleted entry, or NVAL if
   // entry was not found.
   uint64 Delete(uint64 key, uint64 pos = NPOS);

   // Check for index overflow, i.e. the fill factor is above the limit.
   bool overflow() const {
     return header_->size + header_->deletions > limit_;
   }

 private:
  // Magic number and version for identifying database index file.
  static const uint32 MAGIC = 0x46584449;  // IDXF
  static const uint32 VERSION = 1;

  // Special hash values for empty and deleted slots in the index.
  static const uint64 EMPTY = 0;
  static const uint64 DELETED = NVAL;

  // Index file header.
  struct Header {
    uint32 magic;     // magic number for identifying database index file
    uint32 version;   // index file format version
    uint64 offset;    // offset of entry table in index
    uint64 epoch;     // epoch when index was created/updated
    uint64 size;      // number of used entries in index
    uint64 capacity;  // maximum capacity of index
    uint64 deletions; // number of deleted entries in index
  };

  // Index entry.
  struct Entry {
    uint64 hash;      // hash value for entry
    uint64 value;     // value for entry
  };

  // Index file.
  File *file_;

  // Index file mapped to memory.
  char *mapped_;

  // Pointer to index header.
  Header *header_;

  // Pointer to index entries.
  Entry *entries_;

  // Index position mask, i.e. capacity - 1.
  uint64 mask_;

  // Index size limit based on fill factor, i.e. capacity * fill factor.
  uint64 limit_;
};

class Database {
 public:
  Database(const string &idxfile,
           const std::vector<string> &recfiles,
           const RecordFileOptions &options);
  Database(const string &idxfile,
           const string &recfiles,
           const RecordFileOptions &options)
      : Database(idxfile, File::Match(recfiles), options) {}

  ~Database();

 private:
  // Data shard.
  struct Shard {
    RecordReader *reader;
    RecordWriter *writer;
  };

  // Initialize database index.
  void InitializeIndex();

  // Record reader and writer for each shard.
  std::vector<Shard> shards_;

  // Filename for database index.
  string idxfile_;

  // Total size of all data files.
  uint64 datasize_;
};

}  // namespace sling

#endif  // SLING_DB_DB_H_


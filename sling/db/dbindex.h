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

#ifndef SLING_DB_DBINDEX_H_
#define SLING_DB_DBINDEX_H_

#include <string>

#include "sling/base/logging.h"
#include "sling/base/status.h"
#include "sling/base/types.h"
#include "sling/file/file.h"

namespace sling {

// Database index. The index is implemented as a file-backed hash table with
// linear probing. The index allows multiple keys with the same value. Index
// keys 0 and 1 are reserved.
class DatabaseIndex {
 public:
  // Invalid index position.
  const static uint64 NPOS = -1;

  // Invalid value.
  const static uint64 NVAL = -1;

  ~DatabaseIndex() { Close(); }

  // Open existing index file.
  Status Open(const string &filename);

  // Create new index file.
  Status Create(const string &filename, int64 capacity, int64 limit);

  // Flush changes to disk.
  Status Flush(uint64 epoch);

  // Flush and close database index.
  Status Close();

  // Look up value in index. Search for first value if pos is not specified.
  // Otherwise, search for the next value after position. Return the (next)
  // value for the key and update position or NVAL if no match is found.
  uint64 Get(uint64 key, uint64 *pos) const;
  uint64 Get(uint64 key) const {
    uint64 pos = NPOS;
    return Get(key, &pos);
  }

  // Check if key/value pair is in index.
  bool Exists(uint64 key, uint64 value);

  // Add new entry to index. Returns position of new entry.
  uint64 Add(uint64 key, uint64 value);

  // Update value of existing entry. Returns position of updated entry or NPOS
  // if the entry was not found.
  uint64 Update(uint64 key, uint64 oldval, uint64 newval);

  // Delete entry in index. Returns the position for the deleted entry, or NPOS
  // if entry was not found.
  uint64 Delete(uint64 key, uint64 value);

  // Transfer all used index entries to another index.
  void TransferTo(DatabaseIndex *index) const;

  // Copy index from another index. This requires that the other index has the
  // same capacity as this index.
  void CopyFrom(const DatabaseIndex *index);

  // Write index to file.
  Status Write(File *file) const;

  // Check for index overflow, i.e. the fill factor is above the limit.
  bool full() const {
    return header_->size + header_->deletions >= header_->limit;
  }

  // Return current epoch for index.
  uint64 epoch() const { return header_->epoch; }

  // Return capacity of current index.
  uint64 capacity() const { return header_ != nullptr ? header_->capacity : 0; }

  // Return limit for current index.
  uint64 limit() const { return header_ != nullptr ? header_->limit : 0; }

  // Return number of active records.
  uint64 num_records() const { return header_->size - header_->deletions; }

  // Return number of deleted records.
  uint64 num_deleted() const { return header_->deletions; }

  // Error codes.
  enum Errors {
    E_MEMMAP = 2000,         // unable to map index into memory
    E_NOT_INDEX,             // not an index file
    E_NOT_SUPPORTED,         // unsupported index file version
    E_POSITION,              // invalid position of index entries
    E_TRUNCATED,             // index file truncated
    E_CAPACITY,              // index capacity must be power of two
    E_OVERFULL,              // overfull index
    E_LOAD_FACTOR,           // invalid index load factor
    E_ALIGNMENT,             // index capacity not aligned to page size
    E_MISSING,               // index file is missing or empty
  };

 private:
  // Magic number and version for identifying database index file.
  static const uint32 MAGIC = 0x46584449;  // IDXF
  static const uint32 VERSION = 1;

  // Special keys for empty and deleted entries in the index.
  static const uint64 EMPTY = 0;
  static const uint64 TOMBSTONE = 1;

  // Index file header.
  struct Header {
    uint32 magic;     // magic number for identifying database index file
    uint32 version;   // index file format version
    uint64 offset;    // offset of entry table in index
    uint64 epoch;     // epoch when index was created/updated
    uint64 size;      // number of used entries in index
    uint64 capacity;  // maximum capacity of index
    uint64 limit;     // index size limit, i.e. capacity * load factor
    uint64 deletions; // number of deleted entries in index
  };

  // Index entry. If key is EMPTY, the entry is unused, and if the key is
  // TOMBSTONE, the entry has been deleted.
  struct Entry {
    uint64 key;       // key for entry
    uint64 value;     // value for entry
  };

  // Index file.
  File *file_ = nullptr;

  // Index file mapped to memory.
  char *mapped_addr_ = nullptr;

  // Size of index file mapping.
  uint64 mapped_size_;

  // Pointer to index header.
  Header *header_ = nullptr;

  // Pointer to index entries.
  Entry *entries_ = nullptr;

  // Index position mask, i.e. capacity - 1.
  uint64 mask_;
};

}  // namespace sling

#endif  // SLING_DB_DBINDEX_H_


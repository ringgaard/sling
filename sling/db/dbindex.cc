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

#include "sling/db/dbindex.h"

#include <unistd.h>

namespace sling {

// Returns true iff value is a power of 2.
static bool IsPowerOfTwo32(uint64 value) {
  return value && !(value & (value - 1));
}

Status DatabaseIndex::Open(const string &filename) {
  // Open index file.
  Status st = File::Open(filename, "r+", &file_);
  if (!st.ok()) return st;

  // Map index file into memory.
  mapped_size_ = file_->Size();
  mapped_addr_ = static_cast<char *>(file_->MapMemory(0, mapped_size_, true));
  if (mapped_addr_ == nullptr) {
    return Status(E_MEMMAP, "Unable to map index into memory: ", filename);
  }
  header_ = reinterpret_cast<Header *>(mapped_addr_);

  // Check that index is valid.
  if (header_->magic != MAGIC) {
    return Status(E_NOT_INDEX, "Not an index file: ", filename);
  }
  if (header_->version != VERSION) {
    return Status(E_NOT_SUPPORTED, "Unsupported index file version");
  }
  if (header_->offset < sizeof(Header) || header_->offset > mapped_size_) {
    return Status(E_POSITION, "Invalid position of index entries");
  }

  if (header_->offset + header_->capacity * sizeof(Entry) > mapped_size_) {
    return Status(E_TRUNCATED, "Index file truncated");
  }
  if (!IsPowerOfTwo32(header_->capacity)) {
    return Status(E_CAPACITY, "Index capacity must be power of two");
  }
  if (header_->size +  header_->deletions >= header_->capacity) {
    return Status(E_OVERFULL, "Overfull index");
  }
  if (header_->limit >= header_->capacity) {
    return Status(E_LOAD_FACTOR, "Invalid index load factor");
  }

  // Set up index entry table.
  entries_ = reinterpret_cast<Entry *>(mapped_addr_ + header_->offset);
  mask_ = header_->capacity - 1;

  return Status::OK;
}

Status DatabaseIndex::Create(const string &filename,
                             int64 capacity, int64 limit) {
  // Get memory page size.
  uint64 page_size = sysconf(_SC_PAGESIZE);

  // Check that index size is valid.
  if (((capacity * sizeof(Entry)) & page_size) != 0) {
    return Status(E_ALIGNMENT, "Index capacity not aligned to page size");
  }
  if (limit >= capacity) {
    return Status(E_LOAD_FACTOR, "Invalid index load factor");
  }
  if (!IsPowerOfTwo32(header_->capacity)) {
    return Status(E_CAPACITY, "Capacity must be power of two");
  }

  // Create index file.
  Status st = File::Open(filename, "w+", &file_);
  if (!st.ok()) return st;

  // Compute size of index file.
  uint64 offset = 0;
  while (offset < sizeof(Header)) offset += page_size;
  mapped_size_ = offset + capacity * sizeof(Entry);

  // Map index file into memory.
  mapped_addr_ = static_cast<char *>(file_->MapMemory(0, mapped_size_, true));
  if (mapped_addr_ == nullptr) {
    return Status(E_MEMMAP, "Unable to map index into memory: ", filename);
  }
  header_ = reinterpret_cast<Header *>(mapped_addr_);

  // Set up index header.
  header_->magic = MAGIC;
  header_->version = VERSION;
  header_->offset = offset;
  header_->epoch = 0;
  header_->size = 0;
  header_->capacity = capacity;
  header_->limit = limit;
  header_->deletions = 0;

  // Set up index entry table.
  entries_ = reinterpret_cast<Entry *>(mapped_addr_ + offset);
  mask_ = capacity - 1;

  return Status::OK;
}

Status DatabaseIndex::Flush(uint64 epoch) {
  // Flush index table to disk.
  uint64 entry_table_size = header_->capacity * sizeof(Entry);
  Status st = File::FlushMappedMemory(entries_, entry_table_size);
  if (!st.ok()) return st;

  // Update epoch in header and flush it to disk.
  header_->epoch = epoch;
  return File::FlushMappedMemory(header_, header_->offset);
}

Status DatabaseIndex::Close() {
  // Remove memory mapping.
  Status st = File::FreeMappedMemory(mapped_addr_, mapped_size_);
  if (!st.ok()) return st;

  // Close index file.
  st = file_->Close();
  file_ = nullptr;
  return st;
}

uint64 DatabaseIndex::Get(uint64 key, uint64 *pos) const {
  // Compute position of (first) key.
  if (*pos == NPOS) *pos = key & mask_;
  for (;;) {
    Entry &e = entries_[*pos];
    *pos = (*pos + 1) & mask_;
    if (e.key == key) {
      // Return match.
      return e.value;
    } else if (e.key == EMPTY) {
      // Stop when the first empty index slot is found.
      return NVAL;
    }
  }
}

bool DatabaseIndex::Exists(uint64 key, uint64 value) {
  uint64 pos = key & mask_;
  for (;;) {
    Entry &e = entries_[pos];
    if (e.key == key && e.value == value) {
      // Match found.
      return true;
    } else if (e.key == EMPTY) {
      // No match found.
      return false;
    }
    pos = (pos + 1) & mask_;
  }
}

uint64 DatabaseIndex::Add(uint64 key, uint64 value) {
  DCHECK(key != EMPTY && key != TOMBSTONE);
  uint64 pos = key & mask_;
  for (;;) {
    Entry &e = entries_[pos];
    if (e.key == EMPTY || e.key == TOMBSTONE) {
      if (e.key == TOMBSTONE) header_->deletions--;
      e.key = key;
      e.value = value;
      header_->size++;
      return pos;
    }
    pos = (pos + 1) & mask_;
  }
};

uint64 DatabaseIndex::Update(uint64 key, uint64 oldval, uint64 newval) {
  DCHECK(key != EMPTY && key != TOMBSTONE);
  uint64 pos = key & mask_;
  for (;;) {
    Entry &e = entries_[pos];
    if (e.key == key && e.value == oldval) {
      // Match found.
      e.value = newval;
      return pos;
    } else if (e.key == EMPTY) {
      // No match found.
      return NPOS;
    }
    pos = (pos + 1) & mask_;
  }
}

uint64 DatabaseIndex::Delete(uint64 key, uint64 value) {
  DCHECK(key != EMPTY && key != TOMBSTONE);
  uint64 pos = key & mask_;
  for (;;) {
    Entry &e = entries_[pos];
    if (e.key == key && e.value == value) {
      // Match found.
      e.key = TOMBSTONE;
      header_->deletions++;
      header_->size--;
      return pos;
    } else if (e.value == EMPTY) {
      // No match found.
      return NVAL;
    }
    pos = (pos + 1) & mask_;
  }
}

void DatabaseIndex::Transfer(DatabaseIndex *index) const {
  CHECK_GE(index->header_->limit, header_->size);
  Entry *entry = entries_;
  Entry *end = entries_ + header_->capacity;
  while (entry < end) {
    if (entry->key != EMPTY && entry->key != TOMBSTONE) {
      index->Add(entry->key, entry->value);
    }
    entry++;
  }
}

}  // namespace sling


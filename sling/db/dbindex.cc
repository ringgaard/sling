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
  if (mapped_size_ == 0) {
    return Status(E_MISSING, "Missing index file: ", filename);
  }
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
  uint64 page_size = File::PageSize();

  // Check that index size is valid.
  if (((capacity * sizeof(Entry)) & page_size) != 0) {
    return Status(E_ALIGNMENT, "Index capacity not aligned to page size");
  }
  if (limit >= capacity) {
    return Status(E_LOAD_FACTOR, "Invalid index load factor");
  }
  if (!IsPowerOfTwo32(capacity)) {
    return Status(E_CAPACITY, "Capacity must be power of two");
  }

  // Create index file. If filename is empty, a memory index without
  // file-backing is created.
  if (!filename.empty()) {
    // File-backed index.
    Status st = File::Open(filename, "w+", &file_);
    if (!st.ok()) return st;
  } else {
    // Memory-only index.
    file_ = nullptr;
  }

  // Compute size of index file.
  uint64 offset = 0;
  while (offset < sizeof(Header)) offset += page_size;
  mapped_size_ = offset + capacity * sizeof(Entry);

  // Create file mapping.
  if (file_ != nullptr) {
    Status st = file_->Resize(mapped_size_);
    if (!st.ok()) return st;

    // Map index file into memory.
    mapped_addr_ = static_cast<char *>(file_->MapMemory(0, mapped_size_, true));
    if (mapped_addr_ == nullptr) {
      return Status(E_MEMMAP, "Unable to map index into memory: ", filename);
    }
  } else {
    mapped_addr_ = static_cast<char *>(malloc(mapped_size_));
    if (mapped_addr_ == nullptr) {
      return Status(E_MEMMAP, "Unable to allocate memory index");
    }
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
  if (file_ != nullptr) {
    // Flush index table to disk.
    uint64 entry_table_size = header_->capacity * sizeof(Entry);
    Status st = File::FlushMappedMemory(entries_, entry_table_size);
    if (!st.ok()) return st;

    // Update epoch in header and flush it to disk.
    header_->epoch = epoch;
    return File::FlushMappedMemory(header_, header_->offset);
  } else {
    // Just update epoch in header for memory index.
    header_->epoch = epoch;
  }

  return Status::OK;
}

Status DatabaseIndex::Close() {
  if (file_ != nullptr) {
    // Remove memory mapping.
    if (mapped_addr_ != nullptr) {
      Status st = File::FreeMappedMemory(mapped_addr_, mapped_size_);
      if (!st.ok()) return st;
      mapped_addr_ = nullptr;
    }

    // Close index file.
    if (file_ != nullptr) {
      Status st = file_->Close();
      file_ = nullptr;
      if (!st.ok()) return st;
    }
  } else {
    // Deallocate memory index.
    free(mapped_addr_);
    mapped_addr_ = nullptr;
  }

  return Status::OK;
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
      if (e.key == TOMBSTONE) {
        header_->deletions--;
      } else {
        header_->size++;
      }
      e.key = key;
      e.value = value;
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
      return pos;
    } else if (e.value == EMPTY) {
      // No match found.
      return NVAL;
    }
    pos = (pos + 1) & mask_;
  }
}

void DatabaseIndex::TransferTo(DatabaseIndex *index) const {
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

void DatabaseIndex::CopyFrom(const DatabaseIndex *index) {
  // Check that index sizes match.
  CHECK_EQ(mapped_size_, index->mapped_size_);
  CHECK_EQ(mask_, index->mask_);

  // Copy content from the other index.
  memcpy(mapped_addr_, index->mapped_addr_, mapped_size_);
}

Status DatabaseIndex::Write(File *file) const {
  return file->Write(mapped_addr_, mapped_size_);
}

bool DatabaseIndex::Check(bool fix) {
  // Count the number of active entries and tombstones in index.
  Entry *entry = entries_;
  Entry *end = entries_ + header_->capacity;
  uint64 num_used = 0;
  uint64 num_tombstones = 0;
  while (entry < end) {
    if (entry->key == TOMBSTONE) {
      num_tombstones++;
    } else if (entry->key != EMPTY) {
      num_used++;
    }
    entry++;
  }
  uint64 size = num_used + num_tombstones;

  // Check if index size in header is correct.
  if (size != header_->size) {
    LOG(WARNING) << "Index size wrong: " << header_->size
                 << " (" << size << " expected)";
    if (fix) {
      // Correct index size header.
      header_->size = size;
    }
    return false;
  } else {
    return true;
  }
}

}  // namespace sling


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

#include <algorithm>
#include <string>
#include <vector>

#include "sling/util/fingerprint.h"

namespace sling {

Database::~Database() {
  // Close writer.
  delete writer_;

  // Close data files.
  for (RecordReader *reader : readers_) delete reader;

  // Close index.
  if (index_ != nullptr) CHECK(index_->Close());
  delete index_;
}

Status Database::Open(const string &dbdir, bool recover) {
  // Check that database directory exists.
  if (!File::Exists(dbdir)) {
    return Status(E_DB_NOT_FOUND, "Database not found: ", dbdir);
  }
  dbdir_ = dbdir;

  // Get data shards.
  std::vector<string> datafiles;
  File::Match(dbdir_ + "/data-*", &datafiles);
  if (datafiles.empty()) {
    return Status(E_NO_DATA_FILES, "No data files for database: ", dbdir);
  }
  std::sort(datafiles.begin(), datafiles.end());

  // Open readers for all data files.
  for (int i = 0; i < datafiles.size(); ++i) {
    RecordReader *reader = new RecordReader(datafiles[i], config_.record);
    readers_.push_back(reader);
  }

  // The last shard also has a writer for adding records to the database.
  config_.record.append = true;
  writer_ = new RecordWriter(datafiles.back(), config_.record);

  // Open database index.
  index_ = new DatabaseIndex();
  Status st = index_->Open(IndexFile());
  if (!st.ok()) {
    if (recover) {
      LOG(INFO) << "Recover database index for " << dbdir << " due to: " << st;
      uint64 capacity = index_->capacity();
      delete index_;
      index_ = nullptr;
      st = Recover(capacity);
    }
    if (!st.ok()) return st;
  }

  // Check that index is up-to-date.
  if (index_->epoch() != Epoch()) {
    if (recover) {
      LOG(INFO) << "Recover stale database index for " << dbdir;
      uint64 capacity = index_->capacity();
      delete index_;
      index_ = nullptr;
      st = Recover(capacity);
      if (!st.ok()) return st;
    } else {
      return Status(E_STALE_INDEX, "Database index is not up-to-date");
    }
  }

  return Status::OK;
}

Status Database::Create(const string &dbdir, const string &config) {
  // Check that database directory does not exist.
  if (File::Exists(dbdir)) {
    return Status(E_DB_ALREADY_EXISTS, "Database already exists: ", dbdir);
  }
  dbdir_ = dbdir;

  // Create database directory.
  Status st = File::Mkdir(dbdir);
  if (!st.ok()) return st;

  // Write configuration file.
  if (!config.empty()) {
    st = File::WriteContents(dbdir + "/config", config);
    if (!st.ok()) return st;
  }

  // Add initial data shard.
  st = AddDataShard();

  // Create database index.
  index_ = new DatabaseIndex();
  uint64 capacity = config_.initial_index_capacity;
  uint64 limit = capacity * config_.load_factor;
  st = index_->Create(IndexFile(), capacity, limit);
  if (!st.ok()) return st;
  dirty_ = true;

  return Status::OK;
}

Status Database::Flush() {
  if (dirty_) {
    // Flush last data shard.
    if (writer_ != nullptr) {
      Status st = writer_->Flush();
      if (!st.ok()) return st;
    }

    // Flush index to disk.
    if (index_ != nullptr) {
      Status st = index_->Flush(Epoch());
      if (!st.ok()) return st;
    }

    dirty_ = false;
  }

  return Status::OK;
}

bool Database::Get(const Slice &key, Record *record) {
  // Compute record key fingerprint.
  uint64 fp = Fingerprint(key.data(), key.size());

  // Loop over matching records in index.
  uint64 pos = DatabaseIndex::NPOS;
  for (;;) {
    // Get next match in index.
    uint64 recid = index_->Get(fp, &pos);
    if (recid == DatabaseIndex::NVAL) break;

    // Read record from data file.
    ReadRecord(recid, record);

    // Return record if key matches.
    if (key == record->key) return true;
  }

  return false;
}

uint64 Database::Put(const Record &record, bool overwrite) {
  // Compute record key fingerprint.
  uint64 fp = Fingerprint(record.key.data(), record.key.size());

  // Loop over matching records in index to check if there is already a record
  // with a matching key.
  uint64 recid = DatabaseIndex::NVAL;
  uint64 pos = DatabaseIndex::NPOS;
  Record rec;
  for (;;) {
    // Get next match in index.
    recid = index_->Get(fp, &pos);
    if (recid == DatabaseIndex::NVAL) break;

    // Read record from data file and check if keys match.
    ReadRecord(recid, &rec);
    if (rec.key == record.key) break;
  }

  if (recid != DatabaseIndex::NVAL) {
    // Only overwrite existing record if requested.
    if (!overwrite) return DatabaseIndex::NVAL;

    // Check if existing record value matches.
    if (rec.value == record.value) return recid;
  }

  // Make room for more records.
  Expand();

  // Write new record.
  uint64 newid = AppendRecord(record);

  // Update index.
  if (recid == DatabaseIndex::NVAL) {
    // Add new entry to index.
    index_->Add(fp, newid);
  } else {
    // Update existing index entry to point to the new record.
    index_->Update(fp, recid, newid);
  }

  dirty_ = true;
  return newid;
}

bool Database::Delete(const Slice &key) {
  // Compute record key fingerprint.
  uint64 fp = Fingerprint(key.data(), key.size());

  // Loop over matching records in index to find record to delete.
  uint64 recid = DatabaseIndex::NVAL;
  uint64 pos = DatabaseIndex::NPOS;
  Record record;
  for (;;) {
    // Get next match in index.
    recid = index_->Get(fp, &pos);
    if (recid == DatabaseIndex::NVAL) return false;

    // Read record from data file and check if keys match.
    ReadRecord(recid, &record);
    if (key == record.key) break;
  }

  // Make room for more records.
  Expand();

  // Write empty record to mark key as deleted.
  record.key = key;
  record.value = Slice();
  AppendRecord(record);

  // Remove key from index.
  index_->Delete(fp, recid);

  dirty_ = true;
  return true;
}

bool Database::Next(Record *record, uint64 *iterator) {
  uint64 shard = Shard(*iterator);
  uint64 pos = Position(*iterator);
  for (;;) {
    // Check for valid shard.
    if (shard >= readers_.size()) {
      *iterator = -1;
      return false;
    }

    // Flush writer before reading from the last shard.
    if (shard == CurrentShard()) {
      CHECK(writer_->Flush());
      writer_->Sync(readers_.back());
    }

    // Seek to position in shard.
    RecordReader *reader = readers_[shard];
    if (pos == 0) {
      CHECK(reader->Rewind());
      pos = reader->Tell();
    } else {
      CHECK(reader->Seek(pos));
    }

    // Check for end of shard.
    if (reader->Done()) {
      // Next shard.
      shard++;
      pos = 0;
      continue;
    }

    // Read record.
    CHECK(reader->Read(record));
    pos = reader->Tell();

    // Check for deleted record.
    if (record->value.empty()) continue;

    // Check for stale record.
    uint64 recid = RecordID(shard, record->position);
    uint64 fp = Fingerprint(record->key.data(), record->key.size());
    if (!index_->Exists(fp, recid)) continue;

    // Return next record.
    *iterator = RecordID(shard, pos);
    return true;
  }
}

bool Database::Valid(uint64 recid) {
  uint64 shard = Shard(recid);
  uint64 pos = Position(recid);
  if (shard >= readers_.size()) return false;
  if (shard == readers_.size() - 1) {
    if (pos >= writer_->Tell()) return false;
  } else {
    if (pos >= readers_[shard]->size()) return false;
  }
  return true;
}

string Database::DataFile(int shard) const {
  string fn = dbdir_ + "/data-";
  string number = std::to_string(shard);
  for (int z = 0; z < 8 - number.size(); ++z) fn.push_back('0');
  fn.append(number);
  return fn;
}

string Database::IndexFile() const {
  return dbdir_ + "/index";
}

void Database::ReadRecord(uint64 recid, Record *record) {
  uint64 shard = Shard(recid);
  if (shard == CurrentShard()) {
    // Flush writer before reading from the last shard.
    CHECK(writer_->Flush());
    writer_->Sync(readers_.back());
  }
  RecordReader *reader = readers_[shard];
  CHECK(reader->Seek(Position(recid)));
  CHECK(reader->Read(record));
}

uint64 Database::AppendRecord(const Record &record) {
  uint64 pos;
  CHECK(writer_->Write(record, &pos));
  return RecordID(CurrentShard(), pos);
}

Status Database::AddDataShard() {
  // Close current writer.
  if (writer_ != nullptr) {
    Status st = writer_->Close();
    if (!st.ok()) return st;
    delete writer_;
    writer_ = nullptr;
  }

  // Create new empty shard.
  LOG(INFO) << "Add shard " << readers_.size() << " to db " << dbdir_;
  string datafn = DataFile(readers_.size());
  config_.record.append = false;
  writer_ = new RecordWriter(datafn, config_.record);
  Status st = writer_->Flush();
  if (!st.ok()) return st;

  // Create reader for new shard.
  RecordReader *reader = new RecordReader(datafn, config_.record);
  readers_.push_back(reader);
  dirty_ = true;

  return Status::OK;
}

Status Database::ExpandIndex(uint64 capacity) {
  // Rehash index by copying it to a new larger index.
  LOG(INFO) << "Expand index to " << capacity << " entries for db " << dbdir_;
  DatabaseIndex *new_index = new DatabaseIndex();

  // Unlink current index.
  Status st = File::Delete(IndexFile());
  if (!st.ok()) return st;

  // Create new index.
  uint64 limit = capacity * config_.load_factor;
  st = new_index->Create(IndexFile(), capacity, limit);
  if (!st.ok()) return st;

  // Transfer all entries to the new index.
  index_->Transfer(new_index);

  // Switch to new index.
  st = index_->Close();
  if (!st.ok()) return st;

  delete index_;
  index_ = new_index;
  dirty_ = true;
  return Status::OK;
}

void Database::Expand() {
  // Check for data shard overflow.
  if (writer_->Tell() >= config_.data_shard_size) {
    // Switch to writing data to new shard. Closing the writer will transfer
    // ownership of the underlying file to the reader.
    CHECK(writer_->Close());
    delete writer_;
    writer_ = nullptr;

    // Add new data shard.
    CHECK(AddDataShard());
  }

  // Check for index overflow.
  if (index_->full()) {
    // Rehash index by copying it to a new larger index.
    CHECK(ExpandIndex(index_->capacity() * 2));
  }
}

Status Database::Recover(uint64 capacity) {
  // Create new database index.
  if (capacity < config_.initial_index_capacity) {
    capacity = config_.initial_index_capacity;
  }
  uint64 limit = capacity * config_.load_factor;

  index_ = new DatabaseIndex();
  Status st = index_->Create(IndexFile(), capacity, limit);
  if (!st.ok()) return st;

  // Replay all records from the data shards to restore the index.
  Record record;
  for (int shard = 0; shard < readers_.size(); ++shard) {
    LOG(INFO) << "Recover shard " << shard << " of db " << dbdir_;
    RecordReader *reader = readers_[shard];
    st = reader->Rewind();
    if (!st.ok()) return st;
    while (!reader->Done()) {
      // Expand index if needed.
      if (index_->full()) {
        st = ExpandIndex(index_->capacity() * 2);
        if (!st.ok()) return st;
      }

      // Read next record.
      st = reader->Read(&record);
      if (!st.ok()) return st;
      uint64 fp = Fingerprint(record.key.data(), record.key.size());
      uint64 recid = RecordID(shard, record.position);

      // Empty record indicates deletion.
      if (record.value.empty()) {
        index_->Delete(fp, recid);
      } else {
        // Try to locate exising record for key in index.
        uint64 val = DatabaseIndex::NVAL;
        uint64 pos = DatabaseIndex::NPOS;
        string key = record.key.str();
        for (;;) {
          // Get next match in index.
          val = index_->Get(fp, &pos);
          if (val == DatabaseIndex::NVAL) break;

          // Save current position in reader.
          uint64 current = reader->Tell();

          // Read record from data file.
          ReadRecord(val, &record);

          // Restore current position in reader.
          CHECK(reader->Seek(current));

          // Check if key matches.
          if (record.key == Slice(key)) break;
        }

        // If an existing record with the same key is found, update the index
        // entry, otherwise add a new entry.
        if (val == DatabaseIndex::NVAL) {
          index_->Add(fp, recid);
        } else {
          index_->Update(fp, val, recid);
        }
      }
    }
  }

  st = index_->Flush(Epoch());
  if (!st.ok()) return st;

  LOG(INFO) << "Recovery successful for: " << dbdir_;
  dirty_ = false;
  return Status::OK;
}

}  // namespace sling


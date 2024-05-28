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

#include "sling/string/numbers.h"
#include "sling/util/fingerprint.h"

namespace sling {

Database::~Database() {
  // Close writer.
  delete writer_;

  // Close data files.
  for (RecordReader *reader : readers_) delete reader;

  // Close index.
  delete index_;
}

Status Database::Open(const string &dbdir, bool recover) {
  // Check that database directory exists.
  if (!File::Exists(dbdir)) {
    return Status(E_DB_NOT_FOUND, "Database not found: ", dbdir);
  }
  dbdir_ = dbdir;
  datadir_ = dbdir;

  // Read configuration.
  if (File::Exists(ConfigFile())) {
    string config;
    Status st = File::ReadContents(ConfigFile(), &config);
    if (!st.ok()) return st;
    if (!ParseConfig(config)) {
      return Status(E_CONFIG, "Invalid database configuration");
    }
  }

  // Open reader for all data shards.
  std::vector<string> datafiles;
  string last;
  File::Match(dbdir_ + "/data-*", &datafiles);
  for (const string &datafile : datafiles) {
    RecordReader *reader = new RecordReader(datafile, config_.record);
    size_ += reader->size();
    readers_.push_back(reader);
    last = datafile;
  }
  bool last_partition_empty = false;
  for (const string &partition : config_.partitions) {
    if (!File::Exists(partition)) {
      return Status(E_NO_DATA_FILES, "Data partition missing: ", partition);
    }
    datafiles.clear();
    File::Match(partition + "/data-*", &datafiles);
    last_partition_empty = datafiles.empty();
    for (const string &datafile : datafiles) {
      RecordReader *reader = new RecordReader(datafile, config_.record);
      size_ += reader->size();
      readers_.push_back(reader);
      last = datafile;
    }
    datadir_ = partition;
  }

  // The last shard also has a writer for adding records to the database.
  if (!last.empty() && !config_.read_only) {
    config_.record.append = true;
    writer_ = new RecordWriter(last, config_.record);
    size_ -= writer_->Tell();
  }

  // Add new shard to last partition if it is empty.
  if (last_partition_empty && !config_.read_only) {
    Status st = AddDataShard();
    if (!st.ok()) return st;
  }

  // Open database index.
  index_ = new DatabaseIndex();
  if (File::Exists(IndexFile())) {
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
  } else {
    // Index missing, recreate.
    uint64 capacity = config_.initial_index_capacity;
    uint64 limit = capacity * config_.index_load_factor;
    Status st = index_->Create(IndexFile(), capacity, limit);
    if (!st.ok()) return st;
    dirty_ = true;
  }

  // Check that index is up-to-date.
  if (index_->epoch() != epoch()) {
    if (recover) {
      LOG(INFO) << "Recover stale database index for " << dbdir;
      uint64 capacity = index_->capacity();
      delete index_;
      index_ = nullptr;
      Status st = Recover(capacity);
      if (!st.ok()) return st;
    } else {
      return Status(E_STALE_INDEX, "Database index is not up-to-date");
    }
  }

  return Status::OK;
}

Status Database::Create(const string &dbdir, const string &config) {
  // Check that database directory does not already exist.
  if (File::Exists(dbdir)) {
    return Status(E_DB_ALREADY_EXISTS, "Database already exists: ", dbdir);
  }
  dbdir_ = dbdir;

  // Parse database configuration.
  if (!ParseConfig(config)) {
    return Status(E_CONFIG, "Invalid database configuration");
  }

  // Set up data directory.
  datadir_ = dbdir_;
  if (!config_.partitions.empty()) {
    datadir_ = config_.partitions.back();
    if (!File::Exists(datadir_)) {
      return Status(E_NO_DATA_FILES, "Data partition missing: ", datadir_);
    }
  }

  // Create database directory.
  Status st = File::Mkdir(dbdir);
  if (!st.ok()) return st;

  // Write configuration file.
  if (!config.empty()) {
    st = File::WriteContents(dbdir + "/config", config);
    if (!st.ok()) return st;
  }

  // Create database index.
  index_ = new DatabaseIndex();
  uint64 capacity = config_.initial_index_capacity;
  uint64 limit = capacity * config_.index_load_factor;
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

    // Flush index to disk. For a memory-based index, this will just update
    // the epoch in the index header.
    if (index_ != nullptr) {
      Status st = index_->Flush(epoch());
      if (!st.ok()) return st;
    }

    dirty_ = false;
  }

  return Status::OK;
}

Status Database::Bulk(bool enable) {
  if (bulk_ == enable) return Status::OK;
  bulk_ = enable;

  // Switch index. Use memory-based index in bulk mode.
  DatabaseIndex *newidx = new DatabaseIndex();
  Status st = newidx->Create(IndexFile(), index_->capacity(), index_->limit());
  if (!st.ok()) return st;
  newidx->CopyFrom(index_);
  delete index_;
  index_ = newidx;

  // Mark database as dirty when leaving bulk mode to trigger an index flush.
  if (!bulk_) dirty_ = true;

  return Status::OK;
}

Status Database::Backup() {
  // First flush database to ensure we have a consistent state.
  Status st = Flush();
  if (!st.ok()) return st;

  // Write snapshot of index to backup file.
  File *backup;
  st = File::Open(IndexBackupFile(), "w", &backup);
  if (st.ok()) st = index_->Write(backup);
  if (st.ok()) st = backup->Close();
  return st;
}

bool Database::Get(const Slice &key, Record *record, bool novalue) {
  // Compute record key fingerprint.
  inc(GET);
  uint64 fp = Fingerprint(key.data(), key.size());

  // Loop over matching records in index.
  uint64 pos = DatabaseIndex::NPOS;
  for (;;) {
    // Get next match in index.
    uint64 recid = index_->Get(fp, &pos);
    if (recid == DatabaseIndex::NVAL) break;

    // Read record from data file.
    Status st = ReadRecord(recid, record, novalue);
    if (!st) return false;

    // Return record if key matches.
    if (key == record->key) {
      inc(HIT);
      return true;
    }
    inc(MISS);
  }

  return false;
}

uint64 Database::Put(const Record &record, DBMode mode, DBResult *result) {
  // Check if database is read-only.
  if (config_.read_only) return DatabaseIndex::NVAL;

  // The value cannot be empty, since this is used for marking deleted records.
  if (record.value.empty()) return -1;

  // Compute record key fingerprint.
  uint64 fp = Fingerprint(record.key.data(), record.key.size());

  // Loop over matching records in index to check if there is already a record
  // with a matching key.
  inc(PUT);
  uint64 recid = DatabaseIndex::NVAL;
  uint64 pos = DatabaseIndex::NPOS;
  Record rec;
  for (;;) {
    // Get next match in index.
    recid = index_->Get(fp, &pos);
    if (recid == DatabaseIndex::NVAL) break;

    // Read record from data file and check if keys match. In ADD mode we only
    // need the record key.
    Status st = ReadRecord(recid, &rec, mode == DBADD);
    if (!st) {
      if (result != nullptr) *result = DBFAULT;
      return -1;
    }
    if (rec.key == record.key) break;
  }

  if (recid != DatabaseIndex::NVAL) {
    // Do not overwrite exisitng records in ADD mode.
    if (mode == DBADD) {
      if (result != nullptr) *result = DBEXISTS;
      return recid;
    }

    // Do not overwrite newer records in ORDERED mode.
    if (mode == DBORDERED) {
      if (rec.version != 0 && record.version < rec.version) {
        if (result != nullptr) *result = DBSTALE;
        return recid;
      }
    }

    // Only overwrite with newer version number in NEWER mode.
    if (mode == DBNEWER) {
      if (record.version < rec.version) {
        if (result != nullptr) *result = DBSTALE;
        return recid;
      } else if (record.version == rec.version) {
        if (result != nullptr) *result = DBUNCHANGED;
        return recid;
      }
    }

    // Check if existing record value matches.
    if (rec.value == record.value) {
      if (result != nullptr) *result = DBUNCHANGED;
      return recid;
    }
  }

  // Make room for more records.
  Status st = Expand();
  if (!st) return -1;

  // Write new record.
  st = writer_->Write(record, &pos);
  if (!st) return -1;
  uint64 newid = RecordID(CurrentShard(), pos);
  inc(RECWRITE);
  add(BYTEWRITE, record.value.size());

  // Update index.
  if (recid == DatabaseIndex::NVAL) {
    // Add new entry to index.
    index_->Add(fp, newid);
    if (result != nullptr) *result = DBNEW;
  } else {
    // Update existing index entry to point to the new record.
    index_->Update(fp, recid, newid);
    if (result != nullptr) *result = DBUPDATED;
  }

  dirty_ = true;
  return newid;
}

bool Database::Delete(const Slice &key) {
  // Check if database is read-only.
  inc(DELETE);
  if (config_.read_only) return false;

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

    // Read record key from data file and check if keys match.
    Status st = ReadRecord(recid, &record, false);
    if (!st) return false;
    if (key == record.key) break;
  }

  // Make room for more records.
  Status st = Expand();
  if (!st) return false;

  // Write empty record to mark key as deleted.
  record.key = key;
  record.value = Slice();
  st = writer_->Write(record, &pos);
  if (!st) return false;

  // Remove key from index.
  index_->Delete(fp, recid);

  dirty_ = true;
  return true;
}

bool Database::Next(Record *record, uint64 *iterator,
                    bool deletions, bool novalue) {
  inc(NEXT);
  uint64 shard = Shard(*iterator);
  uint64 pos = Position(*iterator);
  for (;;) {
    // Check for valid shard.
    if (shard >= readers_.size()) return false;

    // Flush writer before reading from the last shard.
    if (writer_ != nullptr && shard == CurrentShard()) {
      Status st = writer_->Flush();
      if (!st) return false;
      writer_->Sync(readers_.back());
    }

    // Seek to position in shard.
    RecordReader *reader = readers_[shard];
    if (pos == 0) {
      Status st = reader->Rewind();
      if (!st) return false;
      pos = reader->Tell();
    } else {
      Status st = reader->Seek(pos);
      if (!st) return false;
    }

    // Check for end of shard.
    if (reader->Done()) {
      // Next shard.
      shard++;
      pos = 0;
      continue;
    }

    // Read record.
    if (novalue) {
      Status st = reader->ReadKey(record);
      if (!st) return false;
    } else {
      Status st = reader->Read(record);
      if (!st) return false;
      inc(RECREAD);
      add(BYTEREAD, record->value.size());
    }
    pos = reader->Tell();

    if (record->value.empty()) {
      // Skip deleted record.
      if (!deletions) continue;
    } else {
      // Check for stale record.
      uint64 recid = RecordID(shard, record->position);
      uint64 fp = Fingerprint(record->key.data(), record->key.size());
      if (!index_->Exists(fp, recid)) continue;
    }

    // Return next record.
    *iterator = RecordID(shard, pos);
    return true;
  }
}

bool Database::Valid(uint64 recid) {
  uint64 shard = Shard(recid);
  uint64 pos = Position(recid);
  if (shard >= readers_.size()) return false;
  if (writer_ != nullptr && shard == readers_.size() - 1) {
    if (pos >= writer_->Tell()) return false;
  } else {
    if (pos >= readers_[shard]->size()) return false;
  }
  return true;
}

string Database::ConfigFile() const {
  return dbdir_ + "/config";
}

string Database::IndexFile() const {
  // In bulk mode, an empty index file name means a memory-only index.
  if (bulk_) return "";
  return dbdir_ + "/index";
}

string Database::IndexBackupFile() const {
  return dbdir_ + "/index.bak";
}

string Database::DataFile(int shard) const {
  if (shard < readers_.size()) {
    return readers_[shard]->file()->filename();
  } else {
    string fn = datadir_ + "/data-";
    string number = std::to_string(shard);
    for (int z = 0; z < 8 - number.size(); ++z) fn.push_back('0');
    fn.append(number);
    return fn;
  }
}

Status Database::ReadRecord(uint64 recid, Record *record, bool novalue) {
  Status st;
  uint64 shard = Shard(recid);
  if (writer_ != nullptr && shard == CurrentShard()) {
    // Flush writer before reading from the last shard.
    st = writer_->Flush();
    if (!st) return st;
    writer_->Sync(readers_.back());
  }
  RecordReader *reader = readers_[shard];

  st = reader->Seek(Position(recid));
  if (!st) return st;
  if (novalue) {
    st = reader->ReadKey(record);
  } else {
    st = reader->Read(record);
    add(BYTEREAD, record->value.size());
  }
  inc(RECREAD);
  if (!st) return st;

  return Status::OK;
}

Status Database::AddDataShard() {
  // Close current writer.
  if (writer_ != nullptr) {
    writer_->Sync(readers_.back());
    size_ += writer_->Tell();
    Status st = writer_->Close();
    if (!st.ok()) return st;
    delete writer_;
    writer_ = nullptr;
  }

  // Create new empty shard.
  LOG(INFO) << "Add shard " << readers_.size() << " to db " << dbdir_;
  string datafn = DataFile(readers_.size());
  if (File::Exists(datafn)) {
    return Status(EEXIST, "Shard already exists: ", datafn);
  }
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
  Status st;
  LOG(INFO) << "Expand index to " << capacity << " entries for db " << dbdir_;
  DatabaseIndex *new_index = new DatabaseIndex();

  // Unlink current index.
  if (!bulk_) {
    st = File::Delete(IndexFile());
    if (!st.ok()) return st;
  }

  // Create new index.
  uint64 limit = capacity * config_.index_load_factor;
  st = new_index->Create(IndexFile(), capacity, limit);
  if (!st.ok()) return st;

  // Transfer all entries to the new index.
  index_->TransferTo(new_index);

  // Switch to new index.
  st = index_->Close();
  if (!st.ok()) return st;

  delete index_;
  index_ = new_index;
  dirty_ = true;
  return Status::OK;
}

Status Database::Expand() {
  // Check for data shard overflow.
  if (writer_ == nullptr || writer_->Tell() >= config_.data_shard_size) {
    // Add new data shard.
    Status st = AddDataShard();
    if (!st.ok()) return st;
  }

  // Check for index overflow.
  if (index_->full()) {
    // Rehash index by copying it to a new larger index.
    Status st = ExpandIndex(index_->capacity() * 2);
    if (!st.ok()) return st;
  }

  return Status::OK;
}

Status Database::Recover(uint64 capacity) {
  // Recover index into a memory index first to avoid excessive paging during
  // recovery.
  Status st;
  DatabaseIndex idx;
  CHECK(index_ == nullptr);
  dirty_ = true;

  // Use index backup if available.
  if (File::Exists(IndexBackupFile())) {
    // Copy index backup to memory index.
    DatabaseIndex backup;
    st = backup.Open(IndexBackupFile());
    if (!st.ok()) return st;
    st = idx.Create("", backup.capacity(), backup.limit());
    if (!st.ok()) return st;
    idx.CopyFrom(&backup);
    LOG(INFO) << "Using " << IndexBackupFile() << " for recovery "
              << "starting at " << Position(idx.epoch()) << " in shard "
              << Shard(idx.epoch());
  } else {
    // Create new memory-based database index for recovery.
    if (capacity < config_.initial_index_capacity) {
      capacity = config_.initial_index_capacity;
    }
    uint64 limit = capacity * config_.index_load_factor;
    st = idx.Create("", capacity, limit);
    if (!st.ok()) return st;
    LOG(INFO) << "Recover from scratch with capacity " << capacity;
  }

  // Find starting point for recovery.
  int start_shard = Shard(idx.epoch());
  uint64 start_pos = Position(idx.epoch());

  // Replay all records from the data shards to restore the index.
  uint64 num_recs = 0;
  uint64 num_added = 0;
  uint64 num_deleted = 0;
  uint64 num_updated = 0;
  Record record;
  for (int shard = start_shard; shard < readers_.size(); ++shard) {
    LOG(INFO) << "Recover shard " << shard << " of db " << dbdir_;
    RecordReader reader(DataFile(shard), config_.record);
    if (shard == start_shard && start_pos != 0) {
      st = reader.Seek(start_pos);
      if (!st.ok()) return st;
    }
    while (!reader.Done()) {
      // Expand index if needed.
      if (idx.full()) {
        // Create new index.
        DatabaseIndex newidx;
        uint64 capacity = idx.capacity() * 2;
        uint64 limit = capacity * config_.index_load_factor;
        st = newidx.Create("", capacity, limit);
        if (!st.ok()) return st;

        // Transfer all entries to the new index and switch to new index.
        idx.TransferTo(&newidx);
        st = idx.Close();
        if (!st.ok()) return st;
        std::swap(idx, newidx);
      }

      // Read next record. Only the key is needed, but we read the whole record
      // to stay in read-ahead mode.
      st = reader.Read(&record);
      if (!st.ok()) return st;
      uint64 fp = Fingerprint(record.key.data(), record.key.size());
      uint64 recid = RecordID(shard, record.position);

      // Empty record indicates deletion.
      if (record.value.empty()) {
        idx.Delete(fp, recid);
        num_deleted++;
      } else {
        // Try to locate exising record for key in index.
        uint64 val = DatabaseIndex::NVAL;
        uint64 pos = DatabaseIndex::NPOS;
        for (;;) {
          // Get next match in index.
          val = idx.Get(fp, &pos);
          if (val == DatabaseIndex::NVAL) break;

          // Read existing record key from data file.
          Record existing;
          st = ReadRecord(val, &existing, true);
          if (!st.ok()) return st;

          // Check if key matches.
          if (record.key == existing.key) break;
        }

        // If an existing record with the same key is found, update the index
        // entry, otherwise add a new entry.
        if (val == DatabaseIndex::NVAL) {
          idx.Add(fp, recid);
          num_added++;
        } else {
          idx.Update(fp, val, recid);
          num_updated++;
        }
      }
      if (++num_recs % 1000000 == 0) {
        LOG(INFO) << reader.Tell() << ": "
                  << num_added << " added, "
                  << num_deleted << " deleted, "
                  << num_updated << " updated";
      }
    }
  }

  // Create new index from the memory index.
  index_ = new DatabaseIndex();
  st = index_->Create(IndexFile(), idx.capacity(), idx.limit());
  if (!st.ok()) return st;
  index_->CopyFrom(&idx);
  st = index_->Flush(epoch());
  if (!st.ok()) return st;

  LOG(INFO) << "Recovery successful for: " << dbdir_;
  return Status::OK;
}

static int64 ParseNumber(Text number) {
  int64 scaler = 1;
  if (number.ends_with("K")) {
    scaler = 1LL << 10;
    number.remove_suffix(1);
  } else if (number.ends_with("M")) {
    scaler = 1LL << 20;
    number.remove_suffix(1);
  } else if (number.ends_with("G")) {
    scaler = 1LL << 30;
    number.remove_suffix(1);
  } else if (number.ends_with("T")) {
    scaler = 1LL << 40;
    number.remove_suffix(1);
  }
  int64 n;
  if (!safe_strto64(number.data(), number.size(), &n)) return -1;
  return n * scaler;
}

static float ParseFloat(Text number) {
  float n;
  if (!safe_strtof(number.str(), &n)) return -1.0;
  return n;
}

static bool ParseBool(Text b, bool defval) {
  if (b == "0" || b == "false" || b == "n" || b == "no") return false;
  if (b == "1" || b == "true" || b == "y" || b == "yes") return true;
  return defval;
}

bool Database::ParseConfig(Text config) {
  for (Text &line : config.split('\n')) {
    // Skip comments.
    line = line.trim();
    if (line.empty() || line[0] == '#') continue;

    // Split line into key and value.
    int colon = line.find(':');
    if (colon == -1) {
      LOG(ERROR) << "Colon missing in config line: " << line;
      return false;
    }
    Text key = line.substr(0, colon).trim();
    Text value = line.substr(colon + 1).trim();
    if (key.empty() || value.empty()) {
      LOG(ERROR) << "Bad config line: " << line;
      return false;
    }

    // Update configuration parameter.
    if (key == "data") {
      config_.partitions.push_back(value.str());
    } else if (key == "initial_index_capacity") {
      uint64 n = ParseNumber(value);
      if (n <= 0) {
        LOG(ERROR) << "Invalid capacity: " << line;
        return false;
      }
      config_.initial_index_capacity = n;
    } else if (key == "index_load_factor") {
      float n = ParseFloat(value);
      if (n <= 0.0 || n >= 1.0) {
        LOG(ERROR) << "Invalid load factor: " << line;
        return false;
      }
      config_.index_load_factor = n;
    } else if (key == "data_shard_size") {
      uint64 n = ParseNumber(value);
      if (n <= 0) {
        LOG(ERROR) << "Invalid data shard size: " << line;
        return false;
      }
      config_.data_shard_size = n;
    } else if (key == "buffer_size") {
      int n = ParseNumber(value);
      if (n <= 0) {
        LOG(ERROR) << "Invalid buffer size: " << line;
        return false;
      }
      config_.record.buffer_size = n;
    } else if (key == "chunk_size") {
      int n = ParseNumber(value);
      if (n < 0) {
        LOG(ERROR) << "Invalid chunk size: " << line;
        return false;
      }
      config_.record.chunk_size = n;
    } else if (key == "compression") {
      int n = ParseNumber(value);
      if (n != RecordFile::UNCOMPRESSED && n != RecordFile::SNAPPY) {
        LOG(ERROR) << "Invalid compression: " << line;
        return false;
      }
      config_.record.compression = static_cast<RecordFile::CompressionType>(n);
    } else if (key == "read_only") {
      config_.read_only = ParseBool(value, false);
    } else if (key == "timestamped") {
      config_.timestamped = ParseBool(value, false);
    } else {
      LOG(ERROR) << "Unknown configuration parameter: " << line;
      return false;
    }
  }
  return true;
}

}  // namespace sling


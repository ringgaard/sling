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
#include "sling/base/status.h"
#include "sling/base/types.h"
#include "sling/db/dbindex.h"
#include "sling/file/file.h"
#include "sling/file/recordio.h"
#include "sling/string/text.h"

namespace sling {

// Each database is stored in a separate directory (<dbdir>) with an index file
// (<dbdir>/index) and one or more data shards (<dbdir>/data-99999999). The
// data shards are recordio files and all new records are written sequentially
// to the data files. Record deletion is performed by writing a record with the
// deleted key and an empty value.
class Database {
 public:
  // Write mode for updating records.
  enum Mode {
    OVERWRITE,  // overwrite existing records
    ADD,        // only add new records, do not overwrite existing records
    SERIAL,     // do not overwrite records with newer timestamps
  };

  // Outcome of put operation.
  enum Outcome {
    NEW,        // new record added
    UPDATED,    // existing record updated
    UNCHANGED,  // record not updated because value is unchanged
    EXISTS,     // record already exists and overwrite not allowed
    STALE,      // record not updated because timstamp is not newer
  };

  // Deallocate database instance.
  ~Database();

  // Open existing database. In recovery mode, the index is recreated if it is
  // missing, invalid, or stale.
  Status Open(const string &dbdir, bool recover = false);

  // Create new database.
  Status Create(const string &dbdir, const string &config);

  // Flush changes to database.
  Status Flush();

  // Get record from database. Return true if found.
  bool Get(const Slice &key, Record *record);

  // Add or update record in database. Return record id of new record.
  uint64 Put(const Record &record,
             Mode mode = OVERWRITE,
             Outcome *outcome = nullptr);
  uint64 Add(const Record &record) { return Put(record, ADD); }

  // Delete record in database. Return true if record was deleted.
  bool Delete(const Slice &key);

  // Iterate all active records in database, e.g.
  //   uint64 iterator = 0;
  //   Record record;
  //   while (db->Next(&record, &iterator)) { ... }
  bool Next(Record *record, uint64 *iterator);

  // Check if record id is valid.
  bool Valid(uint64 recid);

  // Return the current epoch for the database.
  uint64 epoch() const { return RecordID(CurrentShard(), writer_->Tell()); }

  // Check if database has unflushed changed.
  bool dirty() const { return dirty_; }

  // Check if database is read-only.
  bool read_only() const { return config_.read_only; }

  // Check if database uses numeric timestamps.
  bool numeric_timestamps() const { return config_.numeric_timestamps; }

  // Return number of active records.
  uint64 num_records() const { return index_->num_records(); }

  // Error codes.
  enum Errors {
    E_DB_NOT_FOUND = 1000,  // database not found
    E_NO_DATA_FILES,        // no data files for database
    E_STALE_INDEX,          // database index is not up-to-date
    E_DB_ALREADY_EXISTS,    // database already exists
    E_CONFIG,               // invalid configuration file
  };

 private:
  // Record IDs encode a shard and a position within the shard. The upper
  // 16 bits are used for the shard, and the lower 48 bits are used for the
  // position.
  static uint64 RecordID(uint64 shard, uint64 position) {
    return shard << 48 | position;
  }
  static uint64 Shard(uint64 recid) {
    return recid >> 48;
  }
  static uint64 Position(uint64 recid) {
    return recid & ((1ULL << 48) - 1);
  }

  // Return the current shard for writing to the database.
  int CurrentShard() const {
    return readers_.size() - 1;
  }

  // Configuration options for database.
  struct Config {
    // Data file configuration.
    RecordFileOptions record;

    // Initial index capacity (default 1M).
    uint64 initial_index_capacity = 1 << 20;

    // Size of each data shard (256 GB).
    uint64 data_shard_size = 256 * (1ULL << 30);

    // Index load factor.
    double index_load_factor = 0.75;

    // Read-only mode.
    bool read_only = false;

    // Timestamp is number instead of time_t.
    bool numeric_timestamps = false;
  };

  // Parse configuration.
  bool ParseConfig(Text config);

  // Return filename for configuation.
  string ConfigFile() const;

  // Return filename for index.
  string IndexFile() const;

  // Return filename for data shard.
  string DataFile(int shard) const;

  // Read data record.
  void ReadRecord(uint64 recid, Record *record);

  // Append data record. Return record id of new record.
  uint64 AppendRecord(const Record &record);

  // Add new empty data shard.
  Status AddDataShard();

  // Expand index to accommodate more entries.
  Status ExpandIndex(uint64 capacity);

  // Expand database for next record.
  void Expand();

  // Recover index from data files.
  Status Recover(uint64 capacity);

  // Database directory.
  string dbdir_;

  // Database configuration.
  Config config_;

  // Record readers for all shards.
  std::vector<RecordReader *> readers_;

  // Record writer for the last shard.
  RecordWriter *writer_ = nullptr;

  // Database index.
  DatabaseIndex *index_ = nullptr;

  // Flag for tracking unwritten changes to database.
  bool dirty_ = false;
};

}  // namespace sling

#endif  // SLING_DB_DB_H_


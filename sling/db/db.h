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
#include "sling/db/dbprotocol.h"
#include "sling/file/file.h"
#include "sling/file/recordio.h"
#include "sling/string/text.h"

namespace sling {

// Each database is stored in a separate directory (<dbdir>) with an index file
// (<dbdir>/index) and one or more data shards (<dbdir>/data-99999999). The
// data shards are recordio files and all new records are written sequentially
// to the data files. Record deletion is performed by writing a record with the
// deleted key and an empty value. Please notice that the database methods are
// not thread-safe and requires synchronized access, e.g. using a mutex.
class Database {
 public:
  // Configuration options for database.
  struct Config {
    // Data file configuration.
    RecordFileOptions record;

    // Directories for data partitions.
    std::vector<string> partitions;

    // Initial index capacity (default 1M).
    uint64 initial_index_capacity = 1 << 20;

    // Size of each data shard (256 GB).
    uint64 data_shard_size = 256 * (1ULL << 30);

    // Index load factor.
    double index_load_factor = 0.75;

    // Read-only mode.
    bool read_only = false;

    // Record version number is timestamp.
    bool timestamped = false;
  };

  // Database performance metrics.
  enum Metric {
    GET,      // number of GET operations
    PUT,      // number of PUT operations
    DELETE,   // number of DELETE operations
    NEXT,     // number of NEXT operations
    READ,     // number of bytes read
    WRITE,    // number of bytes written
    HIT,      // number of hash table hits
    MISS,     // number of hash table misses
  };

  const static int NUM_DBMETRICS = MISS + 1;

  // Deallocate database instance.
  ~Database();

  // Open existing database. In recovery mode, the index is recreated if it is
  // missing, invalid, or stale.
  Status Open(const string &dbdir, bool recover = false);

  // Create new database.
  Status Create(const string &dbdir, const string &config);

  // Flush changes to database.
  Status Flush();

  // Enable or disable bulk mode. In bulk mode, a memory-based index is used to
  // avoid excessive paging during database loading.
  Status Bulk(bool enable);

  // Back up database by making a snapshot of the index.
  Status Backup();

  // Get record from database. Return true if found.
  bool Get(const Slice &key, Record *record, bool with_value = true);

  // Add or update record in database. Return record id of new record.
  uint64 Put(const Record &record,
             DBMode mode = DBOVERWRITE,
             DBResult *result = nullptr);
  uint64 Add(const Record &record) { return Put(record, DBADD); }

  // Delete record in database. Return true if record was deleted.
  bool Delete(const Slice &key);

  // Iterate all (active) records in database, e.g.
  //   uint64 iterator = 0;
  //   Record record;
  //   while (db->Next(&record, &iterator)) { ... }
  bool Next(Record *record, uint64 *iterator,
            bool deletions = false,
            bool with_value = true);

  // Check if record id is valid.
  bool Valid(uint64 recid);

  // Return size of last shard.
  uint64 tail_size() const { return writer_ != nullptr ? writer_->Tell() : 0; }

  // Return total size of data shards.
  uint64 size() const { return size_ + tail_size(); }

  // Return the current epoch for the database.
  uint64 epoch() const {
    return readers_.empty() ? 0 : RecordID(CurrentShard(), tail_size());
  }

  // Check if database has unflushed changed.
  bool dirty() const { return dirty_; }

  // Check if database is read-only.
  bool read_only() const { return config_.read_only; }

  // Use timestamps for record version numbers.
  bool timestamped() const { return config_.timestamped; }

  // Return number of active records.
  uint64 num_records() const { return index_->num_records(); }

  // Return number of deleted records.
  uint64 num_deleted() const { return index_->num_deleted(); }

  // Return number of data shards.
  int num_shards() const { return readers_.size(); }

  // Return index capacity.
  uint64 index_capacity() const { return index_->capacity(); }

  // Return bulk mode.
  bool bulk() const { return bulk_; }

  // Database directory.
  const string &dbdir() const { return dbdir_; }

  // Database configuration.
  const Config &config() const { return config_; }

  // Return database performance counter.
  uint64 counter(Metric metric) const { return counter_[metric]; }

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

  // Increment performance counter.
  void inc(Metric metric) { ++counter_[metric]; }
  void add(Metric metric, uint64 value) { counter_[metric] += value; }

  // Parse configuration.
  bool ParseConfig(Text config);

  // Return filename for configuation.
  string ConfigFile() const;

  // Return filename for index.
  string IndexFile() const;

  // Return filename for index backup.
  string IndexBackupFile() const;

  // Return filename for (new) data shard.
  string DataFile(int shard) const;

  // Read data record (key).
  Status ReadRecord(uint64 recid, Record *record, bool with_value);

  // Add new empty data shard.
  Status AddDataShard();

  // Expand index to accommodate more entries.
  Status ExpandIndex(uint64 capacity);

  // Expand database for next record.
  Status Expand();

  // Recover index from data files.
  Status Recover(uint64 capacity);

  // Increment value for performance counters.

  // Database directory.
  string dbdir_;

  // Directory for new shards.
  string datadir_;

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

  // Bulk mode is used for initial loading of a database.
  bool bulk_ = false;

  // Size of data shards excluding the last one.
  uint64 size_ = 0;

  // Database performance counters.
  uint64 counter_[NUM_DBMETRICS] = {};
};

}  // namespace sling

#endif  // SLING_DB_DB_H_


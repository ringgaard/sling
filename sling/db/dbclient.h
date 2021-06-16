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

#ifndef SLING_DB_DBCLIENT_H_
#define SLING_DB_DBCLIENT_H_

#include <functional>
#include <string>
#include <vector>

#include "sling/base/status.h"
#include "sling/base/types.h"
#include "sling/db/dbprotocol.h"
#include "sling/util/iobuffer.h"

namespace sling {

// Database record with key and value buffers.
struct DBRecord {
  DBRecord() {}
  DBRecord(const Slice &k, const Slice &v) : key(k), value(v) {}

  Slice key;
  Slice value;
  uint64 version = 0;
  DBResult result = DBUNCHANGED;
};

// Database connection to database server. This uses the binary SLINGDB
// protocol to communicate with the database server.
class DBClient {
 public:
  ~DBClient() { Close(); }

  // Connect to database server. The format of the database name is:
  // [<hostname>[:<port>]/]<database name>. The default server is localhost and
  // the default port is 7070.
  Status Connect(const string &database, const string &agent);

  // Close connection to server.
  Status Close();

  // Switch to using another database on server.
  Status Use(const string &dbname);

  // Enable/disable bulk mode for database to avoid excessive checkpointing
  // activity during bulk load of database.
  Status Bulk(bool enable);

  // Get record(s) form database.
  Status Get(const Slice &key, DBRecord *record, IOBuffer *buffer = nullptr);
  Status Get(const std::vector<Slice> &keys,
                   std::vector<DBRecord> *records,
                   IOBuffer *buffer = nullptr);

  // Check record(s) in database. Returns version and length or zero for missing
  // records.
  Status Head(const Slice &key, DBRecord *record);
  Status Head(const std::vector<Slice> &keys,
                    std::vector<DBRecord> *records,
                    IOBuffer *buffer = nullptr);

  // Add or update record(s) in database. The records(s) are updated with the
  // outcome.
  Status Put(DBRecord *record, DBMode mode = DBOVERWRITE);
  Status Put(std::vector<DBRecord> *records, DBMode mode = DBOVERWRITE);
  Status Add(DBRecord *record) { return Put(record, DBADD); }
  Status Add(std::vector<DBRecord> *records) { return Put(records, DBADD); }

  // Delete record in database.
  Status Delete(const Slice &key);

  // Iterate all active records in database, e.g.
  //   uint64 iterator = 0;
  //   Record record;
  //   while (db->Next(&iterator, &record)) { ... }
  Status Next(uint64 *iterator, DBRecord *record);
  Status Next(uint64 *iterator,
              int num,
              uint64 limit,
              bool deletions,
              std::vector<DBRecord> *records,
              IOBuffer *buffer = nullptr);

  // Get current epoch for database. This can be used as the initial iterator
  // value for reading new records from the database.
  Status Epoch(uint64 *epoch);

  // Check if client is connected to database server.
  bool connected() const { return sock_ != -1; }

 private:
  // Database transaction.
  typedef std::function<Status()> Transaction;

  // Write key to request.
  void WriteKey(const Slice &key);

  // Write record to request.
  void WriteRecord(DBRecord *record);

  // Read record from response.
  Status ReadRecord(DBRecord *record, IOBuffer *buffer = nullptr);

  // Read record information from response.
  Status ReadRecordInfo(DBRecord *record, IOBuffer *buffer = nullptr);

  // Execute database operation, reconnecting if connection has been closed.
  Status Transact(Transaction tx);

  // Send request to server and receive reply.
  Status Do(DBVerb verb, IOBuffer *buffer = nullptr);

  // Database name.
  string database_;

  // User agent.
  string agent_;

  // Socket for connection.
  int sock_ = -1;

  // Request and response buffers.
  IOBuffer request_;
  IOBuffer response_;

  // Reply verb from last request.
  DBVerb reply_ = DBOK;
};

}  // namespace sling

#endif  // SLING_DB_DBCLIENT_H_


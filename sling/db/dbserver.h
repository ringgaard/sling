// Copyright 2020 Ringgaard Research ApS
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

#ifndef SLING_DB_DBSERVER_H_
#define SLING_DB_DBSERVER_H_

#include <string>
#include <unordered_map>

#include "sling/base/types.h"
#include "sling/db/db.h"
#include "sling/net/http-server.h"
#include "sling/net/static-content.h"
#include "sling/util/thread.h"

namespace sling {

class DBSession;
class DBMount;
class DBLock;

// HTTP/SLINGDB interface for database engine.
class DBService {
 public:
  // Start database service.
  DBService(const string &dbdir);

  // Stop database service.
  ~DBService();

  // Register database web interface.
  void Register(HTTPServer *http);

  // Mount database.
  Status MountDatabase(const string &name, const string &dbdir, bool recover);

 private:
  // Flush mounted databases.
  void Flush();

  // Checkpoint dirty databases.
  void Checkpoint();

  // Process HTTP database requests.
  void Process(HTTPRequest *request, HTTPResponse *response);

  // Upgrade client protocol.
  void Upgrade(HTTPRequest *request, HTTPResponse *response);

  // Get database record.
  void Get(HTTPRequest *request, HTTPResponse *response);

  // Return single record.
  void ReturnSingle(HTTPResponse *response,
                    const Record &record,
                    bool key, bool timestamp, uint64 next);

  // Return multiple records.
  void ReturnMultiple(HTTPResponse *response, Database *db,
                      uint64 recid, int batch);

  // Get information about database record.
  void Head(HTTPRequest *request, HTTPResponse *response);

  // Add or update database record.
  void Put(HTTPRequest *request, HTTPResponse *response);

  // Delete database record.
  void Delete(HTTPRequest *request, HTTPResponse *response);

  // Return server information.
  void Options(HTTPRequest *request, HTTPResponse *response);

  // Create new database.
  void Create(HTTPRequest *request, HTTPResponse *response);

  // Mount database.
  void Mount(HTTPRequest *request, HTTPResponse *response);

  // Unmount database.
  void Unmount(HTTPRequest *request, HTTPResponse *response);

  // Back up database.
  void Backup(HTTPRequest *request, HTTPResponse *response);

  // Return database statistics.
  void Statusz(HTTPRequest *request, HTTPResponse *response);

  // Check that database name is valid.
  static bool ValidDatabaseName(const string &name);

  // Monitor thread for flushing changes to disk.
  ClosureThread monitor_{[&]() { Checkpoint(); }};

  // Mounted databases.
  std::unordered_map<string, DBMount *> mounts_;

  // Directory for new databases.
  string dbdir_;

  // List of client connections.
  DBSession *clients_ = nullptr;

  // Flag indicating that the database service is terminating.
  bool terminate_ = false;

  // Admin app.
  StaticContent common_{"/common", "app"};
  StaticContent app_{"/adminz", "sling/db/app"};

  // Maximum batch size.
  static const int MAX_BATCH = 1000;

  // Maximum database name size.
  static const int MAX_DBNAME_SIZE = 128;

  // Mutex for accessing global database server state.
  Mutex mu_;

  friend class DBLock;
  friend class DBSession;
};

// Mounted database.
struct DBMount {
  // Initialize database mount.
  DBMount(const string &name);

  // Get exclusive access to mounted database to acquiring the database lock
  // and releasing it again. If the caller is holding the global lock, this
  // will ensure exclusive access.
  void Acquire();

  string name;          // database name
  Database db;          // mounted database
  Mutex mu;             // mutex for serializing access to database
  time_t last_update;   // time of last database update
  time_t last_flush;    // time of last database flush
};

// Lock on database.
class DBLock {
 public:
  // Look up database from URL path and lock it.
  DBLock(DBService *dbs, const char *path);

  // Look up database and lock it.
  DBLock(DBService *dbs, const string &dbname);

  // Lock database.
  DBLock(DBMount *mount);

  // Unlock database.
  ~DBLock();

  // Yield database lock for long-running transactions.
  void Yield();

  DBMount *mount() { return mount_; }
  Database *db() { return &mount_->db; }
  const string &resource() { return resource_; }

 private:
  DBMount *mount_ = nullptr;       // database for resource
  string resource_;                // resource name
};

// Database client connection that uses the binary SLINGDB protocol.
class DBSession : public SocketSession {
 public:
  DBSession(DBService *dbs, SocketConnection *conn, const char *ua);
  ~DBSession() override;

  // Return protocol name.
  const char *Name() override;

  // Return user name.
  const char *Agent() override;

  // Allow long timeout (24 hours) for DB connections.
  int IdleTimeout() override;

  // Process SLINGDB database request.
  Continuation Process(SocketConnection *conn) override;

 private:
  // Switch to using another database.
  Continuation Use();

  // Enable/disable bulk mode for database.
  Continuation Bulk();

  // Get record(s) from database.
  Continuation Get();

  // Check record(s) in database.
  Continuation Head();

  // Add or update database record(s).
  Continuation Put();

  // Delete record(s) from database.
  Continuation Delete();

  // Retrieve the next record(s) for a cursor.
  Continuation Next(int version);

  // Return current epoch for database.
  Continuation Epoch();

  // Return error message to client.
  Continuation Error(const char *msg);

  // Add header to response.
  Continuation Response(DBVerb verb);

  // Read key from request.
  bool ReadKey(Slice *key);

  // Read record from request.
  bool ReadRecord(Record *record);

  // Write record to response.
  void WriteRecord(const Record &record, bool with_value = true);

  DBService *dbs_;                // database server
  SocketConnection *conn_;        // client connection
  DBMount *mount_ = nullptr;      // active database for client
  char *agent_ = nullptr;         // user agent

  // Client list.
  DBSession *next_;
  DBSession *prev_;

  friend class DBService;
};

}  // namespace sling

#endif  // SLING_DB_DBSERVER_H_


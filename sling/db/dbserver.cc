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

#include <signal.h>
#include <time.h>
#include <string>
#include <unistd.h>
#include <unordered_map>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/db/db.h"
#include "sling/db/dbprotocol.h"
#include "sling/net/http-protocol.h"
#include "sling/string/numbers.h"
#include "sling/util/fingerprint.h"
#include "sling/util/thread.h"

DEFINE_int32(port, 7070, "HTTP server port");
DEFINE_string(dbdir, "/var/data/db", "Database directory");
DEFINE_bool(recover, false, "Recover databases when loading");
DEFINE_bool(auto_mount, false, "Automatically mount databases in db dir");

using namespace sling;

// HTTP/SLINGDB interface for database engine.
class DBService {
 public:
  DBService(const string &dbdir) : dbdir_(dbdir) {
    // Start checkpoint monitor.
    monitor_.SetJoinable(true);
    monitor_.Start();
  }

  ~DBService() {
    // Stop checkpoint monitor.
    terminate_ = true;
    monitor_.Join();

    // Flush all changes to disk.
    Flush();

    // Close all mounted databases.
    MutexLock lock(&mu_);
    for (auto &it : mounts_) {
      DBMount *mount = it.second;
      mount->Acquire();
      LOG(INFO) << "Closing database " << mount->name;
      delete mount;
    }
  }

  // Flush mounted databases.
  void Flush() {
    MutexLock lock(&mu_);
    for (auto &it : mounts_) {
      DBMount *mount = it.second;
      mount->Acquire();
      if (mount->db.dirty()) {
        LOG(INFO) << "Flushing database " << mount->name << " to disk";
        Status st = mount->db.Flush();
        if (!st.ok()) {
          LOG(ERROR) << "Shutdown failed for db " << mount->name << ": " << st;
        }
      }
    }
  }

  // Register database web interface.
  void Register(HTTPServer *http) {
    http->Register("/", this, &DBService::Process);
  }

  // Mount database.
  Status Mount(const string &name, const string &dbdir, bool recover) {
    // Open database.
    LOG(INFO) << "Mounting database " << name << " on " << dbdir;
    DBMount *mount = new DBMount(name);
    Status st = mount->db.Open(dbdir, recover);
    if (!st.ok()) {
      delete mount;
      return st;
    }

    // Add database to mount table.
    mounts_[name] = mount;

    // Database mounted sucessfully.
    LOG(INFO) << "Database mounted: " << name << ", "
              << mount->db.num_records() << " records";
    return Status::OK;
  }

  // Process HTTP database requests.
  void Process(HTTPRequest *request, HTTPResponse *response) {
    if (terminate_) {
      response->SendError(500);
      return;
    }

    switch (request->Method()) {
      case HTTP_GET:
        if (strcmp(request->path(), "/") == 0) {
          Upgrade(request, response);
        } else {
          Get(request, response, true);
        }
        break;

      case HTTP_HEAD:
        Get(request, response, false);
        break;

      case HTTP_PUT:
        Put(request, response);
        break;

      case HTTP_DELETE:
        Delete(request, response);
        break;

      case HTTP_OPTIONS:
        Options(request, response);
        break;

      case HTTP_POST: {
        // Perform database command.
        const char *cmd = request->path();
        if (*cmd == '/') cmd++;
        if (strcmp(cmd, "create") == 0) {
          Create(request, response);
        } else if (strcmp(cmd, "mount") == 0) {
          Mount(request, response);
        } else if (strcmp(cmd, "unmount") == 0) {
          Unmount(request, response);
        } else {
          response->SendError(501, nullptr, "Unknown DB command");
        }
        break;
      }

      default:
        response->SendError(405);
    }
  }

  // Upgrade client protocol.
  void Upgrade(HTTPRequest *request, HTTPResponse *response) {
    // Check for upgrade request.
    const char *connection = request->Get("Connection");
    const char *upgrade = request->Get("Upgrade");
    if (connection == nullptr || strcasecmp(connection, "upgrade") != 0 ||
        upgrade == nullptr || strcasecmp(upgrade, "slingdb") != 0) {
      response->SendError(404);
      return;
    }

    // Upgrade to SLINGDB protocol.
    DBClient *client = new DBClient(this, request->conn());
    response->Upgrade(client);
    response->set_status(101);
    response->Set("Connection", "upgrade");
    response->Set("Upgrade", "slingdb");
  }

  // Get database record.
  void Get(HTTPRequest *request, HTTPResponse *response, bool body) {
    // Get database and resource from request.
    DBLock l(this, request->path());
    if (l.mount() == nullptr) {
      response->SendError(404, nullptr, "Database not found");
      return;
    }
    bool timestamped = l.db()->timestamped();

    Record record;
    if (!l.resource().empty()) {
      // Fetch record from database.
      if (!l.db()->Get(l.resource(), &record)) {
        response->SendError(404, nullptr, "Record not found");
        return;
      }

      // Return record.
      ReturnSingle(response, record, body, false, timestamped, -1);
    } else {
      // Read first/next record in iterator.
      URLQuery query(request->query());

      // Get record position.
      uint64 recid = 0;
      Text id = query.Get("id");
      if (!id.empty() && !safe_strtou64(id.data(), id.size(), &recid)) {
        response->SendError(400, nullptr, "Invalid record id");
        return;
      }

      // Get batch size.
      int batch = 1;
      Text n = query.Get("n");
      if (!n.empty()) {
        if (!safe_strto32(n.data(), n.size(), &batch)) {
          response->SendError(400, nullptr, "Invalid batch size");
          return;
        }
      }
      if (batch < 0) batch = 1;
      if (batch > MAX_BATCH) batch = MAX_BATCH;

      if (batch == 1) {
        // Fetch next record from database.
        if (!l.db()->Next(&record, &recid)) {
          response->SendError(404, nullptr, "Record not found");
          return;
        }

        // Return record.
        ReturnSingle(response, record, body, true, timestamped, recid);
      } else {
        // Fetch multiple records.
        ReturnMultiple(response, l.db(), recid, batch, body);
      }
    }
  }

  // Return single record.
  void ReturnSingle(HTTPResponse *response,
                    const Record &record,
                    bool body, bool key, bool timestamp, uint64 next) {
    // Add revision/timestamp if available.
    if (record.version != 0) {
      if (timestamp) {
        char datebuf[RFCTIME_SIZE];
        response->Set("Last-Modified", RFCTime(record.version, datebuf));
      } else {
        response->Set("Version", record.version);
      }
    }

    // Add record key.
    if (key) {
      response->Add("Key", record.key.data(), record.key.size());
    }

    // Add next record id.
    if (next != -1) {
      response->Set("Next", next);
    }

    // Return record value.
    if (body) {
      response->Append(record.value.data(), record.value.size());
    }
  }

  // Return multiple records.
  void ReturnMultiple(HTTPResponse *response, Database *db,
                      uint64 recid, int batch, bool body) {
    string boundary = std::to_string(FingerprintCat(db->epoch(), time(0)));
    Record record;
    uint64 next = -1;
    int num_recs = 0;
    for (int n = 0; n < batch; ++n) {
      // Fetch next record.
      if (!db->Next(&record, &recid)) break;
      next = recid;
      num_recs++;

      // Add MIME part to response.
      response->Append("--");
      response->Append(boundary);
      response->Append("\r\n");

      response->Append("Content-Length: ");
      response->AppendNumber(record.value.size());
      response->Append("\r\n");

      response->Append("Key: ");
      response->Append(record.key.data(), record.key.size());
      response->Append("\r\n");

      if (record.version != 0) {
        if (db->timestamped()) {
          char datebuf[RFCTIME_SIZE];
          response->Append("Last-Modified: ");
          response->Append(RFCTime(record.version, datebuf));
        } else {
          response->Append("Version: ");
          response->AppendNumber(record.version);
        }
        response->Append("\r\n");
      }

      response->Append("\r\n");
      if (body) {
        response->Append(record.value.data(), record.value.size());
      }
    }

    if (next == -1) {
      response->SendError(404, nullptr, "Record not found");
    } else {
      response->Append("--");
      response->Append(boundary);
      response->Append("--\r\n");

      string ct = "multipart/mixed; boundary=" + boundary;
      response->Set("MIME-Version", "1.0");
      response->Set("Content-Type", ct.c_str());
      response->Set("Records", num_recs);
      response->Set("Next", next);
    }
  }

  // Add or update database record.
  void Put(HTTPRequest *request, HTTPResponse *response) {
    // Get database and resource from request.
    DBLock l(this, request->path());
    if (l.mount() == nullptr) {
      response->SendError(404, nullptr, "Database not found");
      return;
    }
    if (l.resource().empty()) {
      response->SendError(400, nullptr, "Record key missing");
      return;
    }
    if (request->content_size() == 0) {
      response->SendError(400, nullptr, "Record value missing");
      return;
    }
    if (l.db()->read_only()) {
      response->SendError(405, nullptr, "Database is read-only");
      return;
    }

    // Get record from request.
    Slice value(request->content(), request->content_size());
    Record record(l.resource(), value);
    if (l.db()->timestamped()) {
      const char *ts = request->Get("Last-Modified");
      if (ts != nullptr) {
        record.version = ParseRFCTime(ts);
        if (record.version == -1) {
          response->SendError(400, nullptr, "Invalid timestamp");
          return;
        }
      }
    } else {
      record.version = request->Get("Version", -1);
      if (record.version == -1) {
        response->SendError(400, nullptr, "Invalid record version");
        return;
      }
    }
    DBMode mode = DBOVERWRITE;
    const char *m = request->Get("Mode");
    if (m != nullptr) {
      if (strcmp(m, "overwrite") == 0) {
        mode = DBOVERWRITE;
      } else if (strcmp(m, "add") == 0) {
        mode = DBADD;
      } else if (strcmp(m, "ordered") == 0) {
        mode = DBORDERED;
      } else if (strcmp(m, "newer") == 0) {
        mode = DBNEWER;
      } else {
        response->SendError(400, nullptr, "Invalid mode");
        return;
      }
    }

    // Add or update record in database.
    DBResult result;
    uint64 recid = l.db()->Put(record, mode, &result);

    // Return error if record could not be written to database.
    if (recid == -1) {
      response->SendError(403, nullptr);
      return;
    }

    // Return result.
    const char *outcome = nullptr;
    switch (result) {
      case DBNEW: outcome = "new"; break;
      case DBUPDATED: outcome = "updated"; break;
      case DBUNCHANGED: outcome = "unchanged"; break;
      case DBEXISTS: outcome = "exists"; break;
      case DBSTALE: outcome = "stale"; break;
    }
    if (outcome != nullptr) response->Set("Result", outcome);

    // Return new record id.
    response->Set("RecordID", recid);

    // Update last modification time.
    l.mount()->last_update = time(0);
  }

  // Delete database record.
  void Delete(HTTPRequest *request, HTTPResponse *response) {
    // Get database and resource from request.
    DBLock l(this, request->path());
    if (l.mount() == nullptr) {
      response->SendError(404, nullptr, "Database not found");
      return;
    }
    if (l.resource().empty()) {
      response->SendError(400, nullptr, "Record key missing");
      return;
    }
    if (l.db()->read_only()) {
      response->SendError(405, nullptr, "Database is read-only");
      return;
    }

    // Delete record.
    if (!l.db()->Delete(l.resource())) {
      response->SendError(404, nullptr, "Record not found");
      return;
    }

    // Update last modification time.
    l.mount()->last_update = time(0);
  }

  // Return server information.
  void Options(HTTPRequest *request, HTTPResponse *response) {
    // Handle ping.
    if (strcmp(request->path(), "*") == 0) {
      response->Set("Allow", "GET, HEAD, PUT, DELETE, POST, OPTIONS");
      return;
    }

    // General server information.
    if (strcmp(request->path(), "/") == 0) {
      response->Append("{\n");

      response->Append("\"databases\": [\n");
      MutexLock lock(&mu_);
      for (auto &it : mounts_) {
        DBMount *mount = it.second;
        mount->Acquire();
        response->Append("  {\"name\": \"");
        response->Append(mount->name);
        response->Append("\", \"records\": ");
        response->AppendNumber(mount->db.num_records());
        response->Append("}\n");
      }
      response->Append("],\n");

      AddNumPair(response, "pid", getpid(), true);

      response->Append("}\n");
      response->set_content_type("text/json");
      return;
    }

    // Database-specific information.
    DBLock l(this, request->path());
    if (l.mount() == nullptr) {
      response->SendError(404, nullptr, "Database not found");
      return;
    }
    response->Append("{\n");
    Database *db = l.db();
    AddPair(response, "name", l.mount()->name);
    AddNumPair(response, "epoch", db->epoch());
    AddPair(response, "dbdir", db->dbdir());
    AddBoolPair(response, "dirty", db->dirty());
    AddBoolPair(response, "read_only", db->read_only());
    AddBoolPair(response, "timestamped", db->timestamped());
    AddNumPair(response, "records", db->num_records());
    AddNumPair(response, "deletions", db->num_deleted());
    AddNumPair(response, "index_capacity", db->index_capacity(), true);
    response->Append("}\n");
    response->set_content_type("text/json");
    response->Set("Epoch", l.db()->epoch());
  }

  // Add key/value pair to response.
  static void AddPair(HTTPResponse *response, const char *key,
                      const string &value, bool last = false) {
    response->Append("\"");
    response->Append(key);
    response->Append("\": \"");
    response->Append(value);
    response->Append("\"");
    if (!last) response->Append(",");
    response->Append("\n");
  }

  static void AddNumPair(HTTPResponse *response, const char *key,
                      int64 value, bool last = false) {
    response->Append("\"");
    response->Append(key);
    response->Append("\": ");
    response->AppendNumber(value);
    if (!last) response->Append(",");
    response->Append("\n");
  }

  static void AddBoolPair(HTTPResponse *response, const char *key,
                          bool value, bool last = false) {
    response->Append("\"");
    response->Append(key);
    response->Append("\": ");
    response->Append(value ? "true" : "false");
    if (!last) response->Append(",");
    response->Append("\n");
  }

  // Create new database.
  void Create(HTTPRequest *request, HTTPResponse *response) {
    // Get parameters.
    URLQuery query(request->query());
    string name = query.Get("name").str();

    // Check that database name is valid.
    if (!ValidDatabaseName(name)) {
      response->SendError(400, nullptr, "Invalid database name");
      return;
    }

    // Check that database mount does not already exist.
    MutexLock lock(&mu_);
    if (mounts_.find(name) != mounts_.end()) {
      response->SendError(500, nullptr, "Database already exists");
      return;
    }

    // Get database configuration from request body.
    string config(request->content(), request->content_size());

    // Create database.
    DBMount *mount = new DBMount(name);
    Status st = mount->db.Create(dbdir_ + "/" + name, config);
    if (!st.ok()) {
      delete mount;
      response->SendError(500, nullptr, HTMLEscape(st.ToString()).c_str());
      return;
    }

    // Add new database to mount table.
    mounts_[name] = mount;

    // Database created sucessfully.
    LOG(INFO) << "Database created: " << name;
    response->SendError(200, nullptr, "Database created");
  }

  // Mount database.
  void Mount(HTTPRequest *request, HTTPResponse *response) {
    // Get parameters.
    URLQuery query(request->query());
    string name = query.Get("name").str();
    bool recover = query.Get("recover", false);

    // Check that database is not already mounted.
    MutexLock lock(&mu_);
    if (mounts_.find(name) != mounts_.end()) {
      response->SendError(500, nullptr, "Database already mounted");
      return;
    }

    // Mount database.
    Status st = Mount(name, dbdir_ + "/" + name, recover);
    if (!st.ok()) {
      response->SendError(500, nullptr, HTMLEscape(st.ToString()).c_str());
      return;
    }

    response->SendError(200, nullptr, "Database mounted");
  }

  // Unmount database.
  void Unmount(HTTPRequest *request, HTTPResponse *response) {
    MutexLock lock(&mu_);

    // Get parameters.
    URLQuery query(request->query());
    string name = query.Get("name").str();

    // Find mounted database.
    auto f = mounts_.find(name);
    if (f == mounts_.end()) {
      response->SendError(404, nullptr, "Database not found");
      return;
    }

    // Acquire database lock to ensure exclusive access.
    DBMount *mount = f->second;
    mount->Acquire();

    // Release database from active clients.
    for (auto *client = clients_; client != nullptr; client = client->next_) {
      if (client->mount_ == mount) client->mount_ = nullptr;
    }

    // Shut down database.
    LOG(INFO) << "Unmounting database: " << name;
    Status st = mount->db.Flush();
    if (!st.ok()) {
      LOG(ERROR) << "Error flushing " << mount->name << ": " << st;
    }
    delete mount;

    // Remove mount from mount table.
    mounts_.erase(f);

    // Database unmounted sucessfully.
    LOG(INFO) << "Database unmounted: " << name;
    response->SendError(200, nullptr, "Database unmounted");
  }

  // Check that database name is valid.
  static bool ValidDatabaseName(const string &name) {
    if (name.empty() || name.size() > MAX_DBNAME_SIZE) return false;
    if (name[0] == '_' || name[0] == '-') return false;
    for (char ch : name) {
      if (ch >= 'A' && ch <= 'Z') continue;
      if (ch >= 'a' && ch <= 'z') continue;
      if (ch >= '0' && ch <= '9') continue;
      if (ch == '_' || ch == '-') continue;
      return false;
    }
    return true;
  }

 private:
  // Mounted database.
  struct DBMount {
    DBMount(const string &name) : name(name) {
      last_update = last_flush = time(0);
    }

    // Get exclusive access to mounted database to acquiring the database lock
    // and releasing it again. If the caller is holding the global lock, this
    // will ensure exclusive access.
    void Acquire() {
      mu.Lock();
      mu.Unlock();
    }

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
    DBLock(DBService *dbs, const char *path) {
      if (path == nullptr) return;

      // Get database name from path.
      if (*path == '/') path++;
      const char *p = path;
      while (*p != 0 && *p != '/') p++;

      // Find mount for database.
      MutexLock lock(&dbs->mu_);
      if (dbs->terminate_) return;
      string dbname = string(path, p - path);
      auto f = dbs->mounts_.find(dbname);
      if (f == dbs->mounts_.end()) return;

      // Lock database.
      mount_ = f->second;
      mount_->mu.Lock();

      // Get resource name from path.
      if (*p == '/') p++;
      if (!DecodeURLComponent(p, &resource_)) resource_.clear();
    }

    // Look up database and lock it.
    DBLock(DBService *dbs, const string &dbname) {
      // Find mount for database.
      MutexLock lock(&dbs->mu_);
      if (dbs->terminate_) return;
      auto f = dbs->mounts_.find(dbname);
      if (f == dbs->mounts_.end()) return;

      // Lock database.
      mount_ = f->second;
      mount_->mu.Lock();
    }

    // Lock database.
    DBLock(DBMount *mount) {
      mount_ = mount;
      if (mount_ != nullptr) mount_->mu.Lock();
    }

    // Unlock database.
    ~DBLock() {
      if (mount_ != nullptr) mount_->mu.Unlock();
    }

    DBMount *mount() { return mount_; }
    Database *db() { return &mount_->db; }
    const string &resource() { return resource_; }

   private:
    DBMount *mount_ = nullptr;       // database for resource
    string resource_;                // resource name
  };

  // Database client connection that uses the binary SLINGDB protocol.
  class DBClient : public SocketSession {
   public:
    DBClient(DBService *dbs, SocketConnection *conn)
        : dbs_(dbs), conn_(conn) {
      // Add client to client list.
      MutexLock lock(&dbs_->mu_);
      next_ = dbs_->clients_;
      prev_ = nullptr;
      if (dbs_->clients_ != nullptr) dbs_->clients_->prev_ = this;
      dbs_->clients_ = this;
   }

    ~DBClient() override {
      // Remove client from client list.
      MutexLock lock(&dbs_->mu_);
      if (prev_ != nullptr) prev_->next_ = next_;
      if (next_ != nullptr) next_->prev_ = prev_;
      if (this == dbs_->clients_) dbs_->clients_ = next_;
    }

    // Return protocol name.
    const char *Name() override { return "DB"; }

    // Process SLINGDB database request.
    Continuation Process(SocketConnection *conn) override {
      // Check if we have received a complete header.
      auto *req = conn->request();
      if (req->available() < sizeof(DBHeader)) return CONTINUE;

      // Check if request body has been received.
      auto *hdr = DBHeader::from(req->begin());
      if (req->available() < hdr->size + sizeof(DBHeader)) return CONTINUE;

      // Dispatch request.
      req->Consume(sizeof(DBHeader));
      if (req->available() != hdr->size) return TERMINATE;
      Continuation cont = TERMINATE;
      switch (hdr->verb) {
        case DBUSE: cont = Use(); break;
        case DBGET: cont = Get(); break;
        case DBPUT: cont = Put(); break;
        case DBDELETE: cont = Delete(); break;
        case DBNEXT: cont = Next(); break;
        default: return Error("command verb not supported");
      }

      // Make sure the whole request has been consumed.
      if (req->available() > 0) req->Consume(req->available());

      return cont;
    }

    // Switch to using another database.
    Continuation Use() {
      auto *req = conn_->request();
      int namelen = req->available();
      string dbname(req->Consume(namelen), namelen);
      DBLock l(dbs_, dbname);
      if (l.mount() == nullptr) return Error("database not found");
      mount_ = l.mount();

      return Response(DBOK);
    }

    // Get record(s) from database.
    Continuation Get() {
      if (mount_ == nullptr) return Error("no database");
      DBLock l(mount_);
      auto *req = conn_->request();
      while (!req->empty()) {
        // Read key for next record.
        Slice key;
        if (!ReadKey(&key)) return TERMINATE;

        // Read record from database.
        Record record;
        if (!l.db()->Get(key, &record)) {
          // Return empty value if record is not found.
          record.key = key;
          record.value.clear();
        }

        // Add record to response.
        WriteRecord(record);
      }

      return Response(DBRECORD);
    }

    // Add or update database record(s).
    Continuation Put() {
      if (mount_ == nullptr) return Error("no database");
      DBLock l(mount_);
      auto *req = conn_->request();
      auto *rsp = conn_->response_body();

      DBMode mode;
      if (!req->Read(&mode, 4)) return TERMINATE;
      if (!ValidDBMode(mode)) return TERMINATE;

      while (!req->empty()) {
        // Read next record.
        Record record;
        if (!ReadRecord(&record)) return TERMINATE;

        // Add/update record in database.
        DBResult result;
        l.db()->Put(record, mode, &result);

        // Return result.
        rsp->Write(&result, 4);
      }

      return Response(DBRESULT);
    }

    // Delete record(s) from database.
    Continuation Delete() {
      if (mount_ == nullptr) return Error("no database");
      DBLock l(mount_);
      auto *req = conn_->request();
      while (!req->empty()) {
        // Read next key.
        Slice key;
        if (!ReadKey(&key)) return TERMINATE;

        // Delete record from database.
        if (!l.db()->Delete(key)) return Error("record not found");
      }

      return Response(DBOK);
    }

    // Retrieve the next record(s) for a cursor.
    Continuation Next() {
      if (mount_ == nullptr) return Error("no database");
      DBLock l(mount_);
      auto *req = conn_->request();
      auto *rsp = conn_->response_body();

      uint64 iterator;
      if (!req->Read(&iterator, 8)) return TERMINATE;
      uint32 num;
      if (!req->Read(&num, 4)) return TERMINATE;

      Record record;
      for (int n = 0; n < num; ++n) {
        // Fetch next record.
        if (!l.db()->Next(&record, &iterator)) {
          if (n == 0) return Response(DBDONE);
          break;
        }
        if (iterator == -1) return Error("error fetching next record");

        // Add record to response.
        WriteRecord(record);
      }

      rsp->Write(&iterator, 8);
      return Response(DBRECORD);
    }

    // Return error message to client.
    Continuation Error(const char *msg) {
      // Clear existing (partial) response.
      conn_->response_header()->Clear();
      conn_->response_body()->Clear();

      // Return error message.
      int msgsize = strlen(msg);
      conn_->response_body()->Write(msg, msgsize);

      return Response(DBERROR);
    }

    // Add header to response.
    Continuation Response(DBVerb verb) {
      DBHeader *hdr = conn_->response_header()->append<DBHeader>();
      hdr->verb = verb;
      hdr->size = conn_->response_body()->available();
      return RESPOND;
    }

    // Read key from request.
    bool ReadKey(Slice *key) {
      auto *req = conn_->request();
      uint32 len;
      if (!req->Read(&len, 4)) return false;
      if (req->available() < len) return false;
      *key = Slice(req->Consume(len), len);
      return true;
    }

    // Read record from request.
    bool ReadRecord(Record *record) {
      // Read key size with version bit.
      auto *req = conn_->request();
      uint32 ksize;
      if (!req->Read(&ksize, 4)) return false;
      bool has_version = ksize & 1;
      ksize >>= 1;

      // Read key.
      if (req->available() < ksize) return false;
      record->key = Slice(req->Consume(ksize), ksize);

      // Optionally read version.
      if (has_version) {
        if (!req->Read(&record->version, 8)) return false;
      } else {
        record->version = 0;
      }

      // Read value size.
      uint32 vsize;
      if (!req->Read(&vsize, 4)) return false;

      // Read value.
      if (req->available() < vsize) return false;
      record->value = Slice(req->Consume(vsize), vsize);

      return true;
    }

    // Write record to response.
    void WriteRecord(const Record &record) {
      auto *rsp = conn_->response_body();
      uint32 ksize = record.key.size() << 1;
      if (record.version != 0) ksize |= 1;
      rsp->Write(&ksize, 4);
      rsp->Write(record.key.data(), record.key.size());

      if (record.version != 0) {
        rsp->Write(&record.version, 8);
      }

      uint32 vsize = record.value.size();
      rsp->Write(&vsize, 4);
      rsp->Write(record.value.data(), record.value.size());
    }

   private:
    DBService *dbs_;                // database server
    SocketConnection *conn_;        // client connection
    DBMount *mount_ = nullptr;      // active database for client

    // Client list.
    DBClient *next_;
    DBClient *prev_;

    friend class DBService;
  };

  // Monitor thread for flushing changes to disk.
  ClosureThread monitor_{[&]() {
    for (;;) {
      // Check for termination.
      if (terminate_) return;

      // Wait until next checkpoint.
      sleep(1);
      if (terminate_) return;

      // Checkpoint databases.
      MutexLock lock(&mu_);
      time_t now = time(0);
      for (auto &it : mounts_) {
        DBMount *mount = it.second;
        if (!mount->db.dirty()) continue;

        // Checkpoint every 60 seconds or after 10 seconds of no activity.
        if (now - mount->last_flush > 60 || now - mount->last_update > 10) {
          mount->Acquire();
          Status st = mount->db.Flush();
          if (!st.ok()) {
            LOG(ERROR) << "Checkpoint failed for " << mount->name << ": " << st;
          }
          mount->last_flush = now;
          VLOG(1) << "Checkpointed " << mount->name
                  << ", " << mount->db.num_records() << " records";
        }
      }
    }
  }};

  // Mounted databases.
  std::unordered_map<string, DBMount *> mounts_;

  // Directory for new databases.
  string dbdir_;

  // List of client connections.
  DBClient *clients_ = nullptr;

  // Flag indicating that the database service is terminating.
  bool terminate_ = false;

  // Maximum batch size.
  static const int MAX_BATCH = 1000;

  // Maximum database name size.
  static const int MAX_DBNAME_SIZE = 128;

  // Mutex for accessing global database server state.
  Mutex mu_;
};

// HTTP server.
HTTPServer *httpd = nullptr;

// Database service.
DBService *dbservice = nullptr;

// Termination handler.
void terminate(int signum) {
  LOG(INFO) << "Shutdown requested";
  if (httpd != nullptr) httpd->Shutdown();
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Initialize database service.
  dbservice = new DBService(FLAGS_dbdir);

  // Mount databases.
  if (FLAGS_auto_mount) {
    std::vector<string> dbdirs;
    File::Match(FLAGS_dbdir + "/*", &dbdirs);
    for (const string &db : dbdirs) {
      string name = db.substr(FLAGS_dbdir.size() + 1);
      CHECK(dbservice->Mount(name, db, FLAGS_recover));
    }
  }

  // Install signal handlers to handle termination.
  signal(SIGTERM, terminate);
  signal(SIGINT, terminate);

  // Start HTTP server.
  LOG(INFO) << "Start HTTP server on port " << FLAGS_port;
  SocketServerOptions sockopts;
  httpd = new HTTPServer(sockopts, FLAGS_port);
  dbservice->Register(httpd);
  CHECK(httpd->Start());
  LOG(INFO) << "Database server running";
  httpd->Wait();

  // Shut down.
  LOG(INFO) << "Shutting down HTTP server";
  delete httpd;
  httpd = nullptr;

  LOG(INFO) << "Shutting down database sever";
  delete dbservice;
  dbservice = nullptr;

  LOG(INFO) << "Done";
  return 0;
}


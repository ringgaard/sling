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
#include "sling/http/http-server.h"
#include "sling/string/numbers.h"
#include "sling/util/fingerprint.h"
#include "sling/util/thread.h"

DEFINE_int32(port, 7070, "HTTP server port");
DEFINE_string(dbdir, "/var/data/db", "Database directory");
DEFINE_bool(recover, false, "Recover databases when loading");
DEFINE_bool(auto_mount, false, "Automatically mount databases in db dir");

using namespace sling;

// HTTP interface for database engine.
class DatabaseService {
 public:
  DatabaseService(const string &dbdir) : dbdir_(dbdir) {
    // Start checkpoint monitor.
    monitor_.SetJoinable(true);
    monitor_.Start();
  }

  ~DatabaseService() {
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
    http->Register("/", this, &DatabaseService::Process);
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

  // Process database requests.
  void Process(HTTPRequest *request, HTTPResponse *response) {
    if (terminate_) {
      response->SendError(500);
      return;
    }

    switch (request->Method()) {
      case HTTP_GET: Get(request, response, true); break;
      case HTTP_HEAD: Get(request, response, false); break;
      case HTTP_PUT: Put(request, response); break;
      case HTTP_DELETE: Delete(request, response); break;
      case HTTP_OPTIONS: Options(request, response); break;

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

  // Get database record.
  void Get(HTTPRequest *request, HTTPResponse *response, bool body) {
    // Get database and resource from request.
    ResourceLock l(this, request->path());
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
      if (batch > max_batch_) batch = max_batch_;

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

  // Return multiple documents.
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
      response->SetContentType(ct.c_str());
      response->Set("Records", num_recs);
      response->Set("Next", next);
    }
  }

  // Add or update database record.
  void Put(HTTPRequest *request, HTTPResponse *response) {
    // Get database and resource from request.
    ResourceLock l(this, request->path());
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
    Database::Mode mode = Database::OVERWRITE;
    const char *m = request->Get("Mode");
    if (m != nullptr) {
      if (strcmp(m, "overwrite") == 0) {
        mode = Database::OVERWRITE;
      } else if (strcmp(m, "add") == 0) {
        mode = Database::ADD;
      } else if (strcmp(m, "ordered") == 0) {
        mode = Database::ORDERED;
      } else {
        response->SendError(400, nullptr, "Invalid mode");
        return;
      }
    }

    // Add or update record in database.
    Database::Outcome outcome;
    uint64 recid = l.db()->Put(record, mode, &outcome);

    // Return error if record could not be written.
    if (recid == -1) {
      response->SendError(403, nullptr);
      return;
    }

    // Return outcome.
    const char *result = nullptr;
    switch (outcome) {
      case Database::NEW: result = "new"; break;
      case Database::UPDATED: result = "updated"; break;
      case Database::UNCHANGED: result = "unchanged"; break;
      case Database::EXISTS: result = "exists"; break;
      case Database::STALE: result = "stale"; break;
    }
    if (result != nullptr) response->Set("Outcome", result);

    // Return new record id.
    response->Set("RecordID", recid);

    // Update last modification time.
    l.mount()->last_update = time(0);
  }

  // Delete database record.
  void Delete(HTTPRequest *request, HTTPResponse *response) {
    // Get database and resource from request.
    ResourceLock l(this, request->path());
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
      response->Append("databases: [\n");
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
      response->Append("]\n");
      response->Append("}\n");
      response->SetContentType("text/json");
      return;
    }

    // Database-specific information.
    ResourceLock l(this, request->path());
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
    response->SetContentType("text/json");
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
    bool valid = name.size() > 0 && name.size() < 128;
    if (name[0] == '_' || name[0] == '-') valid = false;
    for (char ch : name) {
      if (ch >= 'A' && ch <= 'Z') continue;
      if (ch >= 'a' && ch <= 'z') continue;
      if (ch >= '0' && ch <= '9') continue;
      if (ch == '_' || ch == '-') continue;
      valid = false;
    }
    if (!valid) {
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

  // Resource lock on database.
  class ResourceLock {
   public:
    ResourceLock(DatabaseService *dbs, const char *path) {
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

    ~ResourceLock() {
      if (mount_ != nullptr) mount_->mu.Unlock();
    }

    DBMount *mount() { return mount_; }
    Database *db() { return &mount_->db; }
    const string &resource() { return resource_; }

   private:
    DBMount *mount_ = nullptr;       // database for resource
    string resource_;                // resource name
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

  // Flag indicating that the database service is terminating.
  bool terminate_ = false;

  // Maximum batch size.
  const int max_batch_ = 1000;

  // Mutex for accessing global database server state.
  Mutex mu_;
};

// Database service.
DatabaseService *dbservice = nullptr;

// Termination handler.
void terminate(int signum) {
  LOG(INFO) << "Starting shutdown";
  if (dbservice != nullptr) {
    delete dbservice;
    dbservice = nullptr;
  }
  LOG(INFO) << "Shutdown complete";
  exit(signum);
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Initialize database service.
  dbservice = new DatabaseService(FLAGS_dbdir);

  // Install signal handlers to handle termination.
  signal(SIGTERM, terminate);
  signal(SIGINT, terminate);

  // Mount databases.
  if (FLAGS_auto_mount) {
    std::vector<string> dbdirs;
    File::Match(FLAGS_dbdir + "/*", &dbdirs);
    for (const string &db : dbdirs) {
      string name = db.substr(FLAGS_dbdir.size() + 1);
      CHECK(dbservice->Mount(name, db, FLAGS_recover));
    }
  }

  // Start HTTP server.
  LOG(INFO) << "Start HTTP server on port " << FLAGS_port;
  HTTPServerOptions http_opts;
  HTTPServer http(http_opts, FLAGS_port);
  dbservice->Register(&http);
  CHECK(http.Start());
  LOG(INFO) << "Database server running";
  http.Wait();

  // Shut down.
  dbservice->Flush();
  delete dbservice;
  dbservice = nullptr;
  LOG(INFO) << "Done";
  return 0;
}


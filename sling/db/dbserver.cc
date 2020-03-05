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
#include <string>
#include <unistd.h>
#include <unordered_map>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/db/db.h"
#include "sling/http/http-server.h"
#include "sling/string/numbers.h"
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

    // Close all mounted databases.
    MutexLock lock(&mu_);
    for (auto &it : mounts_) {
      DBMount *mount = it.second;
      LOG(INFO) << "Closing database " << mount->name;
      mount->Acquire();
      delete mount;
    }
  }

  // Flush mounted databases.
  void Flush() {
    MutexLock lock(&mu_);
    for (auto &it : mounts_) {
      DBMount *mount = it.second;
      if (mount->db.dirty()) {
        LOG(INFO) << "Flushing database " << mount->name << " to disk";
        mount->Acquire();
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
              << mount->db.num_records() << " records, "
              << mount->db.num_deleted() << " deleted" ;
    return Status::OK;
  }

  // Process database requests.
  void Process(HTTPRequest *request, HTTPResponse *response) {
    switch (request->Method()) {
      case HTTP_GET: Get(request, response, true); break;
      case HTTP_HEAD: Get(request, response, false); break;
      case HTTP_PUT: Put(request, response); break;
      case HTTP_DELETE: Delete(request, response); break;

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

    Record record;
    if (!l.resource().empty()) {
      // Fetch record from database.
      if (!l.db()->Get(l.resource(), &record)) {
        response->SendError(404, nullptr, "Record not found");
        return;
      }
    } else {
      // Read first/next record in iterator.
      URLQuery query(request->query());
      uint64 pos = 0;
      Text id = query.Get("id");
      if (!id.empty() && !safe_strtou64(id.data(), id.size(), &pos)) {
        response->SendError(400, nullptr, "Invalid record id");
        return;
      }

      // Fetch next record from database.
      if (!l.db()->Next(&record, &pos)) {
        response->SendError(404, nullptr, "Record not found");
        return;
      }

      // Add key to response.
      response->Add("Key", record.key.data(), record.key.size());

      // Add next record id to response.
      if (pos != -1) {
        char buffer[kFastToBufferSize];
        char *next = FastUInt64ToBuffer(pos, buffer);
        response->Set("Next", next);
      }
    }

    // Return timestamp if available.
    if (record.timestamp != -1) {
      char datebuf[RFCTIME_SIZE];
      response->Set("Last-Modified", RFCTime(record.timestamp, datebuf));
    }

    // Return record value.
    if (body) {
      response->Append(record.value.data(), record.value.size());
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

    // Add or update record in database.
    Slice value(request->content(), request->content_size());
    Record record(l.resource(), value);
    const char *ts = request->Get("Last-Modified");
    if (ts != nullptr) {
      record.timestamp = ParseRFCTime(ts);
      if (record.timestamp == -1) {
        response->SendError(400, nullptr, "Invalid timestamp");
        return;
      }
    }
    uint64 recid = l.db()->Put(record);

    // Return error if record could not be written.
    if (recid == -1) {
      response->SendError(403, nullptr);
      return;
    }

    // Update last modification time.
    l.mount()->last_update = time(0);

    // Return new record id.
    char buffer[kFastToBufferSize];
    char *id = FastUInt64ToBuffer(recid, buffer);
    response->Set("RecordID", id);
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

    // Delete record.
    if (!l.db()->Delete(l.resource())) {
      response->SendError(404, nullptr, "Record not found");
      return;
    }

    // Update last modification time.
    l.mount()->last_update = time(0);
  }

  // Create new database.
  void Create(HTTPRequest *request, HTTPResponse *response) {
    // Get parameters.
    URLQuery query(request->query());
    string name = query.Get("name").str();

    // Check that database name is valid.
    bool valid = name.size() > 0 && name.size() < 128;
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
    mount->mu.Lock();
    mount->mu.Unlock();

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
          LOG(INFO) << "Checkpointed " << mount->name;
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

  // Mutex for accessing global database server state.
  Mutex mu_;
};

// Database service.
DatabaseService *dbservice = nullptr;

// Termination handler.
void terminate(int signum) {
  LOG(INFO) << "Starting shutdown";
  if (dbservice != nullptr) {
    dbservice->Flush();
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


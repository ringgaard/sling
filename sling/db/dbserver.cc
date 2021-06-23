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

#include "sling/db/dbserver.h"

#include <time.h>
#include <unistd.h>
#include <string>
#include <unordered_map>

#include "sling/db/db.h"
#include "sling/db/dbprotocol.h"
#include "sling/net/http-server.h"
#include "sling/string/numbers.h"
#include "sling/util/fingerprint.h"
#include "sling/util/thread.h"

namespace sling {

DBService::DBService(const string &dbdir) : dbdir_(dbdir) {
  // Start checkpoint monitor.
  monitor_.SetJoinable(true);
  monitor_.Start();
}

DBService::~DBService() {
  // Stop checkpoint monitor.
  VLOG(1) << "Stop checkpoint monitor";
  terminate_ = true;
  monitor_.Join();

  // Flush all changes to disk.
  VLOG(1) << "Flush databases";
  Flush();

  // Close all mounted databases.
  VLOG(1) << "Close all mounted databases";
  MutexLock lock(&mu_);
  for (auto &it : mounts_) {
    DBMount *mount = it.second;
    mount->Acquire();
    VLOG(1) << "Closing database " << mount->name;
    delete mount;
  }
  VLOG(1) << "Database service shut down";
}

void DBService::Register(HTTPServer *http) {
  http->Register("/", this, &DBService::Process);
}

void DBService::Flush() {
  MutexLock lock(&mu_);
  for (auto &it : mounts_) {
    DBMount *mount = it.second;
    mount->Acquire();
    if (mount->db.dirty()) {
      LOG(INFO) << "Flushing database " << mount->name << " to disk";
      Status st = mount->db.Flush();
      if (!st.ok()) {
        LOG(ERROR) << "Flush failed for db " << mount->name << ": " << st;
      }
    }
  }
}

Status DBService::MountDatabase(const string &name,
                                const string &dbdir,
                                bool recover) {
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

void DBService::Checkpoint() {
  for (;;) {
    // Wait until next checkpoint.
    sleep(1);
    if (terminate_) return;

    // Find next database that needs to be flushed.
    mu_.Lock();
    time_t now = time(0);
    DBMount *mount = nullptr;
    for (auto &it : mounts_) {
      DBMount *m = it.second;

      // Only checkpoint dirty databases.
      if (!m->db.dirty()) continue;

      // Checkpoint every five minutes unless database is in bulk mode or
      // after 10 seconds of no activity.
      if (m->db.bulk()) continue;
      if (now - m->last_flush < 300) continue;
      if (now - m->last_update < 10) continue;

      // Select database which has not been flushed for the longest time.
      if (mount != nullptr && mount->last_flush < m->last_flush) continue;
      mount = m;
    }

    if (mount != nullptr) {
      // Flush database.
      DBLock l(mount);
      mu_.Unlock();
      Status st = mount->db.Flush();
      if (!st.ok()) {
        LOG(ERROR) << "Checkpoint failed for " << mount->name << ": " << st;
      }
      mount->last_flush = mount->last_update = time(0);
      VLOG(1) << "Checkpointed " << mount->name
              << ", " << mount->db.num_records() << " records";
    } else {
      mu_.Unlock();
    }
  }
}

void DBService::Process(HTTPRequest *request, HTTPResponse *response) {
  if (terminate_) {
    response->SendError(500);
    return;
  }

  switch (request->Method()) {
    case HTTP_GET:
      if (strcmp(request->path(), "/") == 0) {
        Upgrade(request, response);
      } else {
        Get(request, response);
      }
      break;

    case HTTP_HEAD:
      Head(request, response);
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
      } else if (strcmp(cmd, "backup") == 0) {
        Backup(request, response);
      } else {
        response->SendError(501, nullptr, "Unknown DB command");
      }
      break;
    }

    default:
      response->SendError(405);
  }
}

void DBService::Upgrade(HTTPRequest *request, HTTPResponse *response) {
  // Check for upgrade request.
  const char *connection = request->Get("Connection");
  const char *upgrade = request->Get("Upgrade");
  if (connection == nullptr || strcasecmp(connection, "upgrade") != 0 ||
      upgrade == nullptr || strcasecmp(upgrade, "slingdb") != 0) {
    response->SendError(404);
    return;
  }

  // Upgrade to SLINGDB protocol.
  const char *ua = request->Get("User-Agent");
  DBSession *client = new DBSession(this, request->conn(), ua);
  response->Upgrade(client);
  response->set_status(101);
  response->Set("Connection", "upgrade");
  response->Set("Upgrade", "slingdb");
}

void DBService::Get(HTTPRequest *request, HTTPResponse *response) {
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
    ReturnSingle(response, record, false, timestamped, -1);
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
      ReturnSingle(response, record, true, timestamped, recid);
    } else {
      // Fetch multiple records.
      ReturnMultiple(response, l.db(), recid, batch);
    }
  }
}

void DBService::ReturnSingle(HTTPResponse *response,
                             const Record &record,
                             bool key, bool timestamp, uint64 next) {
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
  response->Append(record.value.data(), record.value.size());
}

// Return multiple records.
void DBService::ReturnMultiple(HTTPResponse *response, Database *db,
                               uint64 recid, int batch) {
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
    response->Append(record.value.data(), record.value.size());
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

void DBService::Head(HTTPRequest *request, HTTPResponse *response) {
  // Get database and resource from request.
  DBLock l(this, request->path());
  if (l.mount() == nullptr) {
    response->set_status(404);
    return;
  }

  // Fetch record information from database.
  Record record;
  if (!l.db()->Get(l.resource(), &record, false)) {
    response->set_status(404);
    return;
  }

  // Return record information.
  response->set_content_length(record.value.size());
  if (record.version != 0) {
    if (l.db()->timestamped()) {
      char datebuf[RFCTIME_SIZE];
      response->Set("Last-Modified", RFCTime(record.version, datebuf));
    } else {
      response->Set("Version", record.version);
    }
  }
}

void DBService::Put(HTTPRequest *request, HTTPResponse *response) {
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
    record.version = request->Get("Version", 0L);
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
    case DBFAULT: outcome = "fault"; break;
  }
  if (outcome != nullptr) response->Set("Result", outcome);

  // Return new record id.
  response->Set("RecordID", recid);

  // Update last modification time.
  l.mount()->last_update = time(0);
}

void DBService::Delete(HTTPRequest *request, HTTPResponse *response) {
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

void DBService::Options(HTTPRequest *request, HTTPResponse *response) {
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
  AddBoolPair(response, "bulk", db->bulk());
  AddBoolPair(response, "read_only", db->read_only());
  AddBoolPair(response, "timestamped", db->timestamped());
  AddNumPair(response, "records", db->num_records());
  AddNumPair(response, "deletions", db->num_deleted());
  AddNumPair(response, "index_capacity", db->index_capacity(), true);
  response->Append("}\n");
  response->set_content_type("text/json");
  response->Set("Epoch", l.db()->epoch());
}

void DBService::AddPair(HTTPResponse *response, const char *key,
                        const string &value, bool last) {
  response->Append("\"");
  response->Append(key);
  response->Append("\": \"");
  response->Append(value);
  response->Append("\"");
  if (!last) response->Append(",");
  response->Append("\n");
}

void DBService::AddNumPair(HTTPResponse *response, const char *key,
                           int64 value, bool last) {
  response->Append("\"");
  response->Append(key);
  response->Append("\": ");
  response->AppendNumber(value);
  if (!last) response->Append(",");
  response->Append("\n");
}

void DBService::AddBoolPair(HTTPResponse *response, const char *key,
                            bool value, bool last) {
  response->Append("\"");
  response->Append(key);
  response->Append("\": ");
  response->Append(value ? "true" : "false");
  if (!last) response->Append(",");
  response->Append("\n");
}

void DBService::Create(HTTPRequest *request, HTTPResponse *response) {
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

void DBService::Mount(HTTPRequest *request, HTTPResponse *response) {
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
  Status st = MountDatabase(name, dbdir_ + "/" + name, recover);
  if (!st.ok()) {
    response->SendError(500, nullptr, HTMLEscape(st.ToString()).c_str());
    return;
  }

  response->SendError(200, nullptr, "Database mounted");
}

void DBService::Unmount(HTTPRequest *request, HTTPResponse *response) {
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

void DBService::Backup(HTTPRequest *request, HTTPResponse *response) {
  // Get parameters.
  URLQuery query(request->query());
  string name = query.Get("name").str();

  // Lock database.
  DBLock l(this, name);
  if (l.mount() == nullptr) {
    response->SendError(404, nullptr, "Database not found");
    return;
  }

  // Back up database.
  LOG(INFO) << "Backing up database: " << name;
  Status st = l.db()->Backup();
  if (!st.ok()) {
    response->SendError(500, nullptr, "Unable to back up database");
    return;
  }

  // Database backup sucessfully.
  LOG(INFO) << "Database backed up: " << name;
  response->SendError(200, nullptr, "Database backed up");
}

bool DBService::ValidDatabaseName(const string &name) {
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

DBMount::DBMount(const string &name) : name(name) {
  last_update = last_flush = time(0);
}

void DBMount::Acquire() {
  mu.Lock();
  mu.Unlock();
}

DBLock::DBLock(DBService *dbs, const char *path) {
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

DBLock::DBLock(DBService *dbs, const string &dbname) {
  // Find mount for database.
  MutexLock lock(&dbs->mu_);
  if (dbs->terminate_) return;
  auto f = dbs->mounts_.find(dbname);
  if (f == dbs->mounts_.end()) return;

  // Lock database.
  mount_ = f->second;
  mount_->mu.Lock();
}

DBLock::DBLock(DBMount *mount) {
  mount_ = mount;
  if (mount_ != nullptr) mount_->mu.Lock();
}

DBLock::~DBLock() {
  if (mount_ != nullptr) mount_->mu.Unlock();
}

void DBLock::Yield() {
  if (mount_ != nullptr) {
    mount_->mu.Unlock();
    mount_->mu.Lock();
  }
}

DBSession::DBSession(DBService *dbs, SocketConnection *conn, const char *ua)
    : dbs_(dbs), conn_(conn) {
  // Add client to client list.
  MutexLock lock(&dbs_->mu_);
  next_ = dbs_->clients_;
  prev_ = nullptr;
  if (dbs_->clients_ != nullptr) dbs_->clients_->prev_ = this;
  dbs_->clients_ = this;
  if (ua) agent_ = strdup(ua);
}

DBSession::~DBSession() {
  // Remove client from client list.
  MutexLock lock(&dbs_->mu_);
  if (prev_ != nullptr) prev_->next_ = next_;
  if (next_ != nullptr) next_->prev_ = prev_;
  if (this == dbs_->clients_) dbs_->clients_ = next_;
  free(agent_);
}

const char *DBSession::Name() {
  return "DB";
}

const char *DBSession::Agent() {
  if (agent_) return agent_;
  if (mount_) return mount_->name.c_str();
  return "";
}

int DBSession::IdleTimeout() {
  return 86400;
}

DBSession::Continuation DBSession::Process(SocketConnection *conn) {
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
    case DBNEXT: cont = Next(1); break;
    case DBBULK: cont = Bulk(); break;
    case DBEPOCH: cont = Epoch(); break;
    case DBHEAD: cont = Head(); break;
    case DBNEXT2: cont = Next(2); break;
    default: return Error("command verb not supported");
  }

  // Make sure the whole request has been consumed.
  if (req->available() > 0) req->Consume(req->available());

  return cont;
}

DBSession::Continuation DBSession::Use() {
  auto *req = conn_->request();
  int namelen = req->available();
  string dbname(req->Consume(namelen), namelen);
  DBLock l(dbs_, dbname);
  if (l.mount() == nullptr) return Error("database not found");
  mount_ = l.mount();

  return Response(DBOK);
}

DBSession::Continuation DBSession::Bulk() {
  if (mount_ == nullptr) return Error("no database");
  DBLock l(mount_);
  auto *req = conn_->request();
  uint32 enable;
  if (!req->Read(&enable, 4)) return TERMINATE;

  Status st = l.db()->Bulk(enable);
  if (!st) return Error("bulk mode cannot be changed");

  if (enable) {
    LOG(INFO) << "Enter bulk mode: " << mount_->name;
  } else {
    LOG(INFO) << "Leave bulk mode: " << mount_->name;
  }

  return Response(DBOK);
}

DBSession::Continuation DBSession::Get() {
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
    l.Yield();
  }

  return Response(DBRECORD);
}

DBSession::Continuation DBSession::Head() {
  if (mount_ == nullptr) return Error("no database");
  DBLock l(mount_);
  auto *req = conn_->request();
  auto *rsp = conn_->response_body();
  while (!req->empty()) {
    // Read key for next record.
    Slice key;
    if (!ReadKey(&key)) return TERMINATE;

    // Get record information from database.
    Record record;
    uint32 vsize = 0;
    if (l.db()->Get(key, &record, false)) {
      vsize = record.value.size();
    }

    // Write record information.
    rsp->Write(&record.version, 8);
    rsp->Write(&vsize, 4);
    l.Yield();
  }

  return Response(DBRECINFO);
}

DBSession::Continuation DBSession::Put() {
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
    if (l.db()->Put(record, mode, &result) == -1) {
      if (record.value.empty()) {
        return Error("record value cannot be empty");
      } else {
        return Error("error writing record");
      }
    }

    // Return result.
    rsp->Write(&result, 4);
    l.Yield();
  }

  l.mount()->last_update = time(0);
  return Response(DBRESULT);
}

DBSession::Continuation DBSession::Delete() {
  if (mount_ == nullptr) return Error("no database");
  DBLock l(mount_);
  auto *req = conn_->request();
  while (!req->empty()) {
    // Read next key.
    Slice key;
    if (!ReadKey(&key)) return TERMINATE;

    // Delete record from database.
    if (!l.db()->Delete(key)) return Error("record not found");
    l.Yield();
  }

  l.mount()->last_update = time(0);
  return Response(DBOK);
}

DBSession::Continuation DBSession::Next(int version) {
  // Supported iteration flags.
  static const uint8 supports =
    DBNEXT_DELETIONS |
    DBNEXT_LIMIT |
    DBNEXT_NOVALUE;

  if (mount_ == nullptr) return Error("no database");
  DBLock l(mount_);
  auto *req = conn_->request();
  auto *rsp = conn_->response_body();

  uint8 flags = 0;
  if (version >= 2) {
    if (!req->Read(&flags, 1)) return TERMINATE;
    if (flags & ~supports) return Error("not supported");
  }
  uint64 iterator;
  if (!req->Read(&iterator, 8)) return TERMINATE;
  uint32 num;
  if (!req->Read(&num, 4)) return TERMINATE;
  uint64 limit = -1;
  if (flags & DBNEXT_LIMIT) {
    if (!req->Read(&limit, 8)) return TERMINATE;
  }
  bool deletions = (flags & DBNEXT_DELETIONS) != 0;
  bool with_value = !(flags & DBNEXT_NOVALUE);

  Record record;
  for (int n = 0; n < num; ++n) {
    // Fetch next record.
    if ((limit != -1 && iterator >= limit) ||
        !l.db()->Next(&record, &iterator, deletions, with_value)) {
      if (n == 0) return Response(DBDONE);
      break;
    }
    if (iterator == -1) return Error("error fetching next record");

    // Add record to response.
    WriteRecord(record, with_value);
    l.Yield();
  }

  rsp->Write(&iterator, 8);
  return Response(with_value ? DBRECORD : DBKEY);
}

DBSession::Continuation DBSession::Epoch() {
  if (mount_ == nullptr) return Error("no database");
  DBLock l(mount_);
  uint64 epoch = l.db()->epoch();
  conn_->response_body()->Write(&epoch, 8);
  return Response(DBRECID);
}

DBSession::Continuation DBSession::Error(const char *msg) {
  // Clear existing (partial) response.
  conn_->response_header()->Clear();
  conn_->response_body()->Clear();

  // Return error message.
  int msgsize = strlen(msg);
  conn_->response_body()->Write(msg, msgsize);

  return Response(DBERROR);
}

DBSession::Continuation DBSession::Response(DBVerb verb) {
  DBHeader *hdr = conn_->response_header()->append<DBHeader>();
  hdr->verb = verb;
  hdr->size = conn_->response_body()->available();
  return RESPOND;
}

bool DBSession::ReadKey(Slice *key) {
  auto *req = conn_->request();
  uint32 len;
  if (!req->Read(&len, 4)) return false;
  if (req->available() < len) return false;
  *key = Slice(req->Consume(len), len);
  return true;
}

bool DBSession::ReadRecord(Record *record) {
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

void DBSession::WriteRecord(const Record &record, bool with_value) {
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
  if (with_value) {
    rsp->Write(record.value.data(), record.value.size());
  }
}

}  // namespace sling


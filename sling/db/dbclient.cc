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

#include "sling/db/dbclient.h"

#include <netdb.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/socket.h>

#include "sling/base/perf.h"

namespace sling {

// Return truncation error.
static Status Truncated() {
  return Status(EBADMSG, "packet truncated");
}

Status DBClient::Connect(const string &database, const string &agent) {
  // Parse database specification.
  database_ = database;
  agent_ = agent;
  string hostname = "localhost";
  string portname = "7070";
  string dbname;
  int slash = database.find('/');
  if (slash == -1) {
    dbname = database;
  } else {
    dbname = database.substr(slash + 1);
    if (slash > 0) {
      hostname = database.substr(0, slash);
      int colon = hostname.find(':');
      if (colon != -1) {
        portname = hostname.substr(colon + 1);
        hostname.resize(colon);
      }
    }
  }

  // Connect to database server.
  Status st = Client::Connect(hostname, portname, "slingdb", agent);
  if (!st.ok()) return st;

  // Switch to database.
  if (!dbname.empty()) {
    return Use(dbname);
  } else {
    return Status::OK;
  }
}

Status DBClient::Use(const string &dbname) {
  request_.Clear();
  request_.Write(dbname);
  return Do(DBUSE);
}

Status DBClient::Bulk(bool enable) {
  return Transact([&]() -> Status {
    uint32 value = enable;
    request_.Clear();
    request_.Write(&value, 4);
    return Do(DBBULK);
  });
}

Status DBClient::Get(const Slice &key, DBRecord *record, IOBuffer *buffer) {
  if (buffer == nullptr) buffer = &response_;
  return Transact([&]() -> Status {
    request_.Clear();
    WriteKey(key);
    Status st = Do(DBGET, buffer);
    if (!st.ok()) return st;
    return ReadRecord(record, buffer, false);
  });
}

Status DBClient::Get(const std::vector<Slice> &keys,
                     std::vector<DBRecord> *records,
                     IOBuffer *buffer) {
  if (buffer == nullptr) buffer = &response_;
  return Transact([&]() -> Status {
    request_.Clear();
    for (auto &key : keys) WriteKey(key);
    Status st = Do(DBGET, buffer);
    if (!st.ok()) return st;
    records->resize(keys.size());
    for (int i = 0; i < keys.size(); ++i) {
      Status st = ReadRecord(&records->at(i), buffer, false);
      if (!st.ok()) return st;
    }
    return Status::OK;
  });
}

Status DBClient::Head(const Slice &key, DBRecord *record) {
  return Transact([&]() -> Status {
    request_.Clear();
    WriteKey(key);
    Status st = Do(DBHEAD);
    if (!st.ok()) return st;
    record->key = key;
    return ReadRecordInfo(record, &response_);
  });
}

Status DBClient::Head(const std::vector<Slice> &keys,
                      std::vector<DBRecord> *records,
                      IOBuffer *buffer) {
  if (buffer == nullptr) buffer = &response_;
  return Transact([&]() -> Status {
    request_.Clear();
    for (auto &key : keys) WriteKey(key);
    Status st = Do(DBHEAD, buffer);
    if (!st.ok()) return st;
    records->resize(keys.size());
    for (int i = 0; i < keys.size(); ++i) {
      records->at(i).key = keys[i];
      Status st = ReadRecordInfo(&records->at(i), buffer);
      if (!st.ok()) return st;
    }
    return Status::OK;
  });
}

Status DBClient::Put(DBRecord *record, DBMode mode) {
  return Transact([&]() -> Status {
    request_.Clear();
    request_.Write(&mode, 4);
    WriteRecord(record);
    Status st = Do(DBPUT);
    if (!st.ok()) return st;
    if (!response_.Read(&record->result, 4)) return Truncated();
    return Status::OK;
  });
}

Status DBClient::Put(std::vector<DBRecord> *records, DBMode mode) {
  return Transact([&]() -> Status {
    request_.Clear();
    request_.Write(&mode, 4);
    for (auto &record : *records) WriteRecord(&record);
    Status st = Do(DBPUT);
    if (!st.ok()) return st;
    for (int i = 0; i < records->size(); ++i) {
      if (!response_.Read(&records->at(i).result, 4)) return Truncated();
    }
    return Status::OK;
  });
}

Status DBClient::Delete(const Slice &key) {
  return Transact([&]() -> Status {
    request_.Clear();
    WriteKey(key);
    return Do(DBDELETE);
  });
}

Status DBClient::Next(DBIterator *iterator, DBRecord *record) {
  return Transact([&]() -> Status {
    CHECK_EQ(iterator->batch, 1);
    uint8 flags = 0;
    if (iterator->deletions) flags |= DBNEXT_DELETIONS;
    if (iterator->limit != -1) flags |= DBNEXT_LIMIT;
    if (iterator->novalue) flags |= DBNEXT_NOVALUE;
    request_.Write(&flags, 1);
    request_.Write(&iterator->position, 8);
    request_.Write(&iterator->batch, 4);
    if (iterator->limit != -1) request_.Write(&iterator->limit, 8);
    Status st = Do(DBNEXT2, iterator->buffer);
    if (!st.ok()) return st;
    if (reply_ == DBDONE) return Status(ENOENT, "No more records");
    st = ReadRecord(record, iterator->buffer, iterator->novalue);
    if (!st.ok()) return st;
    if (!response_.Read(&iterator->position, 8)) return Truncated();
    return Status::OK;
  });
}

Status DBClient::Next(DBIterator *iterator, std::vector<DBRecord> *records) {
  IOBuffer *buffer = iterator->buffer ? iterator->buffer : &response_;
  return Transact([&]() -> Status {
    records->clear();
    request_.Clear();
    uint8 flags = 0;
    if (iterator->deletions) flags |= DBNEXT_DELETIONS;
    if (iterator->limit != -1) flags |= DBNEXT_LIMIT;
    if (iterator->novalue) flags |= DBNEXT_NOVALUE;
    request_.Write(&flags, 1);
    request_.Write(&iterator->position, 8);
    request_.Write(&iterator->batch, 4);
    if (iterator->limit != -1) request_.Write(&iterator->limit, 8);
    Status st = Do(DBNEXT2, buffer);
    if (!st.ok()) return st;
    if (reply_ == DBDONE) return Status(ENOENT, "No more records");
    DBRecord record;
    while (buffer->available() > 8) {
      st = ReadRecord(&record, buffer, iterator->novalue);
      if (!st.ok()) return st;
      records->push_back(record);
    }
    if (!buffer->Read(&iterator->position, 8)) return Truncated();
    return Status::OK;
  });
}

Status DBClient::Stream(DBIterator *iterator, Callback cb) {
  IOBuffer *buffer = iterator->buffer ? iterator->buffer : &response_;
  uint8 flags = 0;
  if (iterator->deletions) flags |= DBNEXT_DELETIONS;
  if (iterator->limit != -1) flags |= DBNEXT_LIMIT;
  if (iterator->novalue) flags |= DBNEXT_NOVALUE;
  request_.Write(&flags, 1);
  request_.Write(&iterator->position, 8);
  if (iterator->limit != -1) request_.Write(&iterator->limit, 8);

  Status st = Send(DBSTREAM, &request_);
  if (!st.ok()) return st;

  DBRecord record;
  bool done = false;
  while (!done) {
    st = Receive(buffer);
    if (!st.ok()) return st;
    if (reply_ == DBEND) {
      done = true;
      if (!buffer->Read(&iterator->position, 8)) return Truncated();
    } else if (reply_ == DBDATA) {
      st = ReadRecord(&record, buffer, iterator->novalue);
      if (!st.ok()) return st;
      st = cb(record);
      if (!st.ok()) return st;
    } else if (reply_ == DBERROR) {
      int size = buffer->available();
      return Status(EINVAL, buffer->Consume(size), size);
    } else {
      return Status(EBADMSG, "bad db packet");
    }
  }
  return Status::OK;
}

Status DBClient::Epoch(uint64 *epoch) {
  return Transact([&]() -> Status {
    request_.Clear();
    Status st = Do(DBEPOCH);
    if (!st.ok()) return st;
    if (reply_ != DBRECID) return Status(ENOSYS, "Not supported");
    if (!response_.Read(epoch, 8)) return Truncated();
    return Status::OK;
  });
}

Status DBClient::Clear() {
  return Transact([&]() -> Status {
    request_.Clear();
    Status st = Do(DBCLEAR);
    if (!st.ok()) return st;
    return Status::OK;
  });
}

void DBClient::WriteKey(const Slice &key) {
  uint32 size = key.size();
  request_.Write(&size, 4);
  request_.Write(key);
}

void DBClient::WriteRecord(DBRecord *record) {
  uint32 ksize = record->key.size() << 1;
  if (record->version != 0) ksize |= 1;
  request_.Write(&ksize, 4);
  request_.Write(record->key);

  if (record->version != 0) {
    request_.Write(&record->version, 8);
  }

  uint32 vsize = record->value.size();
  request_.Write(&vsize, 4);
  request_.Write(record->value);
}

Status DBClient::ReadRecord(DBRecord *record, IOBuffer *buffer, bool novalue) {
  // Use internal reponse buffer if none has been supplied.
  if (buffer == nullptr) buffer = &response_;

  // Read key size with version bit.
  uint32 ksize;
  if (!buffer->Read(&ksize, 4)) return Truncated();
  bool has_version = ksize & 1;
  ksize >>= 1;

  // Read key.
  if (buffer->available() < ksize) return Truncated();
  record->key = Slice(buffer->Consume(ksize), ksize);

  // Optionally read version.
  if (has_version) {
    if (!buffer->Read(&record->version, 8)) return Truncated();
  } else {
    record->version = 0;
  }

  // Read value size.
  uint32 vsize;
  if (!buffer->Read(&vsize, 4)) return Truncated();

  // Read value.
  if (novalue) {
    char *bad = reinterpret_cast<char *>(0xDECADE0FABBABABE);
    record->value = Slice(bad, vsize);
  } else {
    if (buffer->available() < vsize) return Truncated();
    record->value = Slice(buffer->Consume(vsize), vsize);
  }

  return Status::OK;
}

Status DBClient::ReadRecordInfo(DBRecord *record, IOBuffer *buffer) {
  // Use internal reponse buffer if none has been supplied.
  if (buffer == nullptr) buffer = &response_;

  // Read version.
  if (!buffer->Read(&record->version, 8)) return Truncated();

  // Read size.
  int32 vsize;
  if (!buffer->Read(&vsize, 4)) return Truncated();

  char *bad = reinterpret_cast<char *>(0xDECADE0FABBABABE);
  record->value = Slice(bad, vsize);

  return Status::OK;
}

Status DBClient::Transact(Transaction tx) {
  // Execute operation.
  Status st = tx();

  // Reconnect if connection closed.
  if (st.code() == EPIPE) {
    VLOG(1) << "Reconnect to " << database_;
    Close();
    st = Connect(database_, agent_);
    if (st.ok()) st = tx();
  }

  return st;
}

Status DBClient::Do(DBVerb verb, IOBuffer *response) {
  if (response == nullptr) response = &response_;
  Status st = Perform(verb, &request_, response);
  if (!st.ok()) return st;

  // Check for server errors.
  if (reply_ == DBERROR) {
    int size = response->available();
    return Status(EINVAL, response->Consume(size), size);
  }

  return Status::OK;
}

}  // namespace sling

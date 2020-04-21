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

namespace sling {

// Return system error.
static Status Error(const char *context) {
  return Status(errno, context, strerror(errno));
}

// Return truncation error.
static Status Truncated() {
  return Status(EBADMSG, "packet truncated");
}

Status DBClient::Connect(const string &database) {
  // Parse database specification.
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

  // Look up server address and connect.
  struct addrinfo hints = {}, *addrs;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  int err = getaddrinfo(hostname.c_str(), portname.c_str(), &hints, &addrs);
  if (err != 0) return Status(err, gai_strerror(err), hostname);

  for(struct addrinfo *addr = addrs; addr != nullptr; addr = addr->ai_next) {
    // Create socket.
    sock_ = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (sock_ == -1) {
      err = errno;
      break;
    }

    // Connect socket.
    if (connect(sock_, addr->ai_addr, addr->ai_addrlen) == 0) break;
    err = errno;
    close(sock_);
    sock_ = -1;
  }
  freeaddrinfo(addrs);
  if (sock_ == -1) return Status(err, strerror(err), database);

  // Upgrade connection from HTTP to SLINGDB protocol.
  request_.Clear();
  request_.Write(
    "GET / HTTP/1.1\r\n"
    "Host: " + hostname + "\r\n"
    "Connection: upgrade\r\n"
    "Upgrade: slingdb\r\n"
    "\r\n");
  int rc = send(sock_, request_.begin(), request_.available(), 0);
  if (rc < 0) return Error("send");
  if (rc != request_.available()) {
    return Status(EBADE, "Upgrade failed in send");
  }

  response_.Clear();
  while (response_.available() < 12 ||
         memcmp(response_.end() - 4, "\r\n\r\n", 4) != 0) {
    response_.Ensure(256);
    int rc = recv(sock_, response_.end(), response_.remaining(), 0);
    if (rc < 0) return Error("recv");
    if (rc == 0) return Status(EBADE, "Upgrade failed in recv");
    response_.Append(rc);
  }
  if (!response_.data().starts_with("HTTP/1.1 101")) {
    return Status(EBADE, "Upgrade failed");
  }

  // Switch to database.
  if (!dbname.empty()) {
    return Use(dbname);
  } else {
    return Status::OK;
  }
}

Status DBClient::Close() {
  if (sock_ != -1) {
    if (close(sock_) != 0) return Error("close");
    sock_ = -1;
  }
  return Status::OK;
}

Status DBClient::Use(const string &dbname) {
  request_.Clear();
  request_.Write(dbname);
  return Do(DBUSE);
}

Status DBClient::Bulk(bool enable) {
  uint32 value = enable;
  request_.Clear();
  request_.Write(&value, 4);
  return Do(DBBULK);
}

Status DBClient::Get(const Slice &key, DBRecord *record) {
  request_.Clear();
  WriteKey(key);
  Status st = Do(DBGET);
  if (!st.ok()) return st;
  return ReadRecord(record);
}

Status DBClient::Get(const std::vector<Slice> &keys,
                     std::vector<DBRecord> *records) {
  request_.Clear();
  for (auto &key : keys) WriteKey(key);
  Status st = Do(DBGET);
  if (!st.ok()) return st;
  records->resize(keys.size());
  for (int i = 0; i < keys.size(); ++i) {
    Status st = ReadRecord(&records->at(i));
    if (!st.ok()) return st;
  }
  return Status::OK;
}

Status DBClient::Put(DBRecord *record, DBMode mode) {
  request_.Clear();
  request_.Write(&mode, 4);
  WriteRecord(record);
  Status st = Do(DBPUT);
  if (!st.ok()) return st;
  if (!response_.Read(&record->result, 4)) return Truncated();
  return Status::OK;
}

Status DBClient::Put(std::vector<DBRecord> *records, DBMode mode) {
  request_.Clear();
  request_.Write(&mode, 4);
  for (auto &record : *records) WriteRecord(&record);
  Status st = Do(DBPUT);
  if (!st.ok()) return st;
  for (int i = 0; i < records->size(); ++i) {
    if (!response_.Read(&records->at(i).result, 4)) return Truncated();
  }
  return Status::OK;
}

Status DBClient::Delete(const Slice &key) {
  request_.Clear();
  WriteKey(key);
  return Do(DBDELETE);
}

Status DBClient::Next(uint64 *iterator, DBRecord *record) {
  uint32 num = 1;
  request_.Clear();
  request_.Write(iterator, 8);
  request_.Write(&num, 4);
  Status st = Do(DBNEXT);
  if (!st.ok()) return st;
  if (reply_ == DBDONE) return Status(ENOENT, "No more records");
  st = ReadRecord(record);
  if (!st.ok()) return st;
  if (!response_.Read(iterator, 8)) return Truncated();
  return Status::OK;
}

Status DBClient::Next(uint64 *iterator, int num,
                      std::vector<DBRecord> *records) {
  records->clear();
  request_.Clear();
  request_.Write(iterator, 8);
  request_.Write(&num, 4);
  Status st = Do(DBNEXT);
  if (!st.ok()) return st;
  if (reply_ == DBDONE) return Status(ENOENT, "No more records");
  DBRecord record;
  while (response_.available() > 8) {
    st = ReadRecord(&record);
    if (!st.ok()) return st;
    records->push_back(record);
  }
  if (!response_.Read(iterator, 8)) return Truncated();
  return Status::OK;
}

Status DBClient::Epoch(uint64 *epoch) {
  request_.Clear();
  Status st = Do(DBEPOCH);
  if (!st.ok()) return st;
  if (reply_ != DBRECID) return Status(ENOSYS, "Not supported");
  if (!response_.Read(epoch, 8)) return Truncated();
  return Status::OK;
}

void DBClient::WriteKey(const Slice &key) {
  uint32 size = key.size();
  request_.Write(&size, 4);
  request_.Write(key.data(), key.size());
}

void DBClient::WriteRecord(DBRecord *record) {
  uint32 ksize = record->key.size() << 1;
  if (record->version != 0) ksize |= 1;
  request_.Write(&ksize, 4);
  request_.Write(record->key.data(), record->key.size());

  if (record->version != 0) {
    request_.Write(&record->version, 8);
  }

  uint32 vsize = record->value.size();
  request_.Write(&vsize, 4);
  request_.Write(record->value.data(), record->value.size());
}

Status DBClient::ReadRecord(DBRecord *record) {
  // Read key size with version bit.
  uint32 ksize;
  if (!response_.Read(&ksize, 4)) return Truncated();
  bool has_version = ksize & 1;
  ksize >>= 1;

  // Read key.
  if (response_.available() < ksize) return Truncated();
  record->key = Slice(response_.Consume(ksize), ksize);

  // Optionally read version.
  if (has_version) {
    if (!response_.Read(&record->version, 8)) return Truncated();
  } else {
    record->version = 0;
  }

  // Read value size.
  uint32 vsize;
  if (!response_.Read(&vsize, 4)) return Truncated();

  // Read value.
  if (response_.available() < vsize) return Truncated();
  record->value = Slice(response_.Consume(vsize), vsize);

  return Status::OK;
}

Status DBClient::Do(DBVerb verb) {
  // Send request.
  DBHeader reqhdr;
  reqhdr.verb = verb;
  reqhdr.size = request_.available();

  size_t reqsize = request_.available();
  size_t bufsize = sizeof(DBHeader) + reqsize;
  iovec buf[2];
  buf[0].iov_base = &reqhdr;
  buf[0].iov_len = sizeof(DBHeader);
  buf[1].iov_base = request_.Consume(reqsize);
  buf[1].iov_len = reqsize;

  int rc = writev(sock_, buf, 2);
  if (rc == 0) return Status(EIO, "Connection closed");
  if (rc < 0) return Error("send");
  if (rc != bufsize) return Status(EMSGSIZE, "Send truncated");

  // Receive response.
  response_.Clear();
  response_.Ensure(sizeof(DBHeader));
  int size = -1;
  while (size < 0 || response_.available() < size) {
    int rc = recv(sock_, response_.end(), response_.remaining(), 0);
    if (rc < 0) return Error("recv");
    if (rc == 0) return Status(EIO, "Connection closed");
    response_.Append(rc);
    if (size < 0 && response_.available() >= sizeof(DBHeader)) {
      auto *hdr = DBHeader::from(response_.begin());
      size = sizeof(DBHeader) + hdr->size;
      response_.Ensure(size);
    }
  }
  if (response_.available() > size) {
    return Status(EMSGSIZE, "Response too long");
  }

  // Consume header.
  DBHeader *rsphdr = response_.consume<DBHeader>();
  reply_ = rsphdr->verb;

  // Check for errors.
  if (reply_ == DBERROR) {
    return Status(EINVAL, response_.Consume(rsphdr->size), rsphdr->size);
  }

  return Status::OK;
}

}  // namespace sling


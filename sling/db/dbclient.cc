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
#include <sys/socket.h>

namespace sling {

// Return system error.
static Status Error(const char *context) {
  return Status(errno, context, strerror(errno));
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

Status DBClient::Do(int verb) {
  // Send header.
  DBHeader reqhdr;
  reqhdr.verb = verb;
  reqhdr.size = request_.available();
  int rc = send(sock_, &reqhdr, sizeof(reqhdr), 0);
  if (rc == 0) return Status(EIO, "Connection closed");
  if (rc != sizeof(reqhdr)) return Error("send");

  // Send body.
  while (!request_.empty()) {
    int rc = send(sock_, request_.begin(), request_.available(), 0);
    if (rc == 0) return Status(EIO, "Connection closed");
    if (rc < 0) return Error("send");
    request_.Consume(rc);
  }

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
    return Status(EOVERFLOW, "Response too long");
  }

  // Consume header.
  auto *rsphdr = DBHeader::from(response_.Consume(sizeof(DBHeader)));
  reply_ = rsphdr->verb;

  // Check for errors.
  if (reply_ == DBERROR) {
    return Status(EINVAL, response_.Consume(rsphdr->size), rsphdr->size);
  }

  return Status::OK;
}

}  // namespace sling


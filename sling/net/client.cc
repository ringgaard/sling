// Copyright 2025 Ringgaard Research ApS
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

#include "sling/net/client.h"

#include <netdb.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/socket.h>

#include "sling/base/perf.h"

namespace sling {

// Return system error.
static Status Error(const char *context) {
  return Status(errno, context, strerror(errno));
}

Status Client::Connect(const string &hostname,
                       const string &portname,
                       const string &protocol,
                       const string &agent) {
  // Close existing connection.
  if (sock_ != -1) {
    close(sock_);
    sock_ = -1;
  }

  // Look up server address and connect.
  struct addrinfo hints = {}, *addrs;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  int err = getaddrinfo(hostname.c_str(), portname.c_str(), &hints, &addrs);
  if (err != 0) return Status(err, gai_strerror(err), hostname);

  for (struct addrinfo *addr = addrs; addr != nullptr; addr = addr->ai_next) {
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
  if (sock_ == -1) return Status(err, strerror(err), hostname);

  // Upgrade connection.
  string request =  "GET / HTTP/1.1\r\n"
    "Host: " + hostname + "\r\n"
    "User-Agent: " + agent + "\r\n"
    "Connection: upgrade\r\n"
    "Upgrade: " + protocol + "\r\n"
    "\r\n";
  int rc = send(sock_, request.data(), request.size(), 0);
  if (rc < 0) return Error("send");
  if (rc != request.size()) {
    return Status(EBADE, "Upgrade failed in send");
  }

  IOBuffer response;
  while (response.available() < 12 ||
         memcmp(response.end() - 4, "\r\n\r\n", 4) != 0) {
    response.Ensure(256);
    int rc = recv(sock_, response.end(), response.remaining(), 0);
    if (rc < 0) return Error("recv");
    if (rc == 0) return Status(EBADE, "Upgrade failed in recv");
    response.Append(rc);
  }
  if (!response.data().starts_with("HTTP/1.1 101")) {
    return Status(EBADE, "Upgrade failed");
  }

  return Status::OK;
}

Status Client::Close() {
  if (sock_ != -1) {
    if (close(sock_) != 0) return Error("close");
    sock_ = -1;
  }
  return Status::OK;
}

Status Client::Perform(uint32 verb, IOBuffer *request, IOBuffer *response) {
  // Send request.
  Status st = Send(verb, request);
  if (!st.ok()) return st;

  // Receive response.
  return Receive(response);
}

Status Client::Send(uint32 verb, IOBuffer *request) {
  Header hdr;
  hdr.verb = verb;
  hdr.size = request->available();

  size_t reqsize = request->available();
  size_t bufsize = sizeof(Header) + reqsize;
  iovec buf[2];
  buf[0].iov_base = &hdr;
  buf[0].iov_len = sizeof(Header);
  buf[1].iov_base = request->Consume(reqsize);
  buf[1].iov_len = reqsize;

  int rc = writev(sock_, buf, 2);
  if (rc == 0) return Status(EPIPE, "Connection closed");
  if (rc < 0) return Error("send");
  if (rc != bufsize) return Status(EMSGSIZE, "Send truncated");
  Perf::add_network_transmit(rc);

  return Status::OK;
}

Status Client::Receive(IOBuffer *response) {
  Header hdr;
  char *data = reinterpret_cast<char *>(&hdr);
  int left = sizeof(Header);
  while (left > 0) {
    int rc = recv(sock_, data, left, 0);
    if (rc == 0) return Status(EPIPE, "Connection closed");
    if (rc < 0) return Error("recv");
    data += rc;
    Perf::add_network_receive(rc);
    left -= rc;
  }
  reply_ = hdr.verb;

  response->Clear();
  response->Ensure(hdr.size);
  left = hdr.size;
  while (left > 0) {
    int rc = recv(sock_, response->end(), left, 0);
    if (rc == 0) return Status(EPIPE, "Connection closed");
    if (rc < 0) return Error("recv");
    response->Append(rc);
    Perf::add_network_receive(rc);
    left -= rc;
  }

  return Status::OK;
}

}  // namespace sling

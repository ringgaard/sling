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

#ifndef SLING_NET_CLIENT_H_
#define SLING_NET_CLIENT_H_

#include <string>
#include <vector>

#include "sling/base/status.h"
#include "sling/base/types.h"
#include "sling/util/iobuffer.h"

namespace sling {

// Simple binary packet protocol running over a HTTP socket connection. It uses
// the HTTP upgrade mechanism to switch to the binary protocol. Each packet
// starts with a small header with a 32 bit command/response verb and a 32-bit
// payload length.
class Client {
 public:
  ~Client() { Close(); }

  // Connect to server.
  Status Connect(const string &hostname,
                 const string &portname,
                 const string &protocol,
                 const string &agent);

  // Close connection to server.
  Status Close();

  // Check if client is connected to server.
  bool connected() const { return sock_ != -1; }

 protected:
  // Packet header.
  struct Header {
    uint32 verb;   // command or reply type
    uint32 size;   // size of packet body

    static Header *from(char *buf) { return reinterpret_cast<Header *>(buf); }
  };

  // Send request to server and receive reply.
  Status Perform(uint32 verb, IOBuffer *request, IOBuffer *response) const;

  // Send request to server.
  Status Send(uint32 verb, IOBuffer *request) const;

  // Receive response from server.
  Status Receive(IOBuffer *response) const;

  // Socket for connection.
  int sock_ = -1;

  // Reply verb from last request.
  mutable uint32 reply_ = 0;
};

}  // namespace sling

#endif  // SLING_NET_CLIENT_H_

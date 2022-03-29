// Copyright 2022 Ringgaard Research ApS
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

#ifndef SLING_NET_WEB_SOCKETS_H_
#define SLING_NET_WEB_SOCKETS_H_

#include "sling/net/socket-server.h"
#include "sling/net/http-server.h"

namespace sling {

// Web Socket connection.
class WebSocket : public SocketSession {
 public:
  // WebSocket operations.
  enum WSOp {
    WS_OP_CONT  = 0x00,
    WS_OP_TEXT  = 0x01,
    WS_OP_BIN   = 0x02,
    WS_OP_CLOSE = 0x08,
    WS_OP_PING  = 0x09,
    WS_OP_PONG  = 0x0A,
  };

  // Initialize WebSocket protocol handler.
  WebSocket(SocketConnection *conn);

  // Socket session interface.
  const char *Name() override;
  int IdleTimeout() override;
  Continuation Process(SocketConnection *conn) override;

  // Upgrade socket session to Web Sockets. Return true if upgraded.
  static bool Upgrade(WebSocket *websocket,
                      HTTPRequest *request,
                      HTTPResponse *response);

  // Web socket interface.
  virtual void Receive(const uint8 *data, uint64 size, bool binary) = 0;
  virtual void Ping(const void *data, size_t size);
  virtual void Close();

  // Send frame to client.
  void Send(int type, const void *data, size_t size);
  void Send(const Slice &packet) {
    Send(WS_OP_BIN, packet.data(), packet.size());
  }
  void Send(const void *data, size_t size) {
    Send(WS_OP_BIN, data, size);
  }
  void SendText(const char *data, size_t size) {
    Send(WS_OP_TEXT, data, size);
  }

  // Last time event was received on connection.
  time_t last() const { return conn_->last(); }

 private:
  // Socket connection to client.
  SocketConnection *conn_;
};

}  // namespace sling

#endif  // SLING_NET_WEB_SOCKETS_H_


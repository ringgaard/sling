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

#include <string.h>

#include "sling/net/web-sockets.h"
#include "third_party/sha1/sha1.h"

namespace sling {

// Get 16-bit network-order integer.
static uint16 ntoh16(const uint8 *data) {
  return (data[0] << 8) | (data[1] << 0);
}

// Get 64-bit network-order integer.
static uint64 ntoh64(const uint8 *data) {
  return (static_cast<uint64>(data[0]) << 56) |
         (static_cast<uint64>(data[1]) << 48) |
         (static_cast<uint64>(data[2]) << 40) |
         (static_cast<uint64>(data[3]) << 32) |
         (static_cast<uint64>(data[4]) << 24) |
         (static_cast<uint64>(data[5]) << 16) |
         (static_cast<uint64>(data[6]) <<  8) |
         (static_cast<uint64>(data[7]) <<  0);
}

bool WebSocket::Upgrade(WebSocket *websocket,
                        HTTPRequest *request,
                        HTTPResponse *response) {
  // Check for websocket upgrade.
  const char *connection = request->Get("Connection");
  const char *upgrade = request->Get("Upgrade");
  if (connection == nullptr) return false;
  if (strcasecmp(connection, "upgrade") != 0) return false;
  if (upgrade == nullptr) return false;
  if (strcasecmp(upgrade, "websocket") != 0) return false;

  // Upgrade to WebSocket.
  response->Upgrade(websocket);
  response->set_status(101);
  response->Set("Connection", "upgrade");
  response->Set("Upgrade", "websocket");

  // Compute response key.
  const char *key = request->Get("Sec-WebSocket-Key");
  if (key) {
    sha1_context ctx;
    char response_key[SHA1_BASE64_LENGTH];
    sha1_start(&ctx);
    sha1_strupdate(&ctx, key, strlen(key));
    sha1_strupdate(&ctx, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", 36);
    sha1_finish_base64(&ctx, response_key);
    response->Set("Sec-WebSocket-Accept", response_key);
  }
  return true;
}

WebSocket::WebSocket(SocketConnection *conn) : conn_(conn) {}

const char *WebSocket::Name() {
  return "WebSocket";
}

int WebSocket::IdleTimeout() {
  return 86400;  // 24 hours timeout
}

WebSocket::Continuation WebSocket::Process(SocketConnection *conn) {
  // Check if we have received a complete frame header.
  auto *req = conn->request();
  int available = req->available();
  if (available < 2) return CONTINUE;
  const uint8 *hdr = reinterpret_cast<const uint8 *>(req->begin());
  int hdrlen;
  switch (hdr[1]) {
    case 0x7E: hdrlen =  4; break; // 16-bit payload length without masking
    case 0x7F: hdrlen = 10; break; // 64-bit payload length without masking
    case 0xFE: hdrlen =  8; break; // 16-bit payload length with masking
    case 0xFF: hdrlen = 14; break; // 64-bit payload length with masking
    default: // 7-bit payload length
      hdrlen = hdr[1] & 0x80 ? 6 : 2;
  }
  if (available < hdrlen) return CONTINUE;

  // Clear masking key.
  uint8 mask[4];
  memset(mask, 0, 4);

  // Get payload length and masking key.
  size_t datalen;
  switch (hdr[1]) {
    case 0x7E: // 16-bit payload length without masking
      datalen = ntoh16(hdr + 2);
      break;
    case 0x7F: // 64-bit payload length without masking
      datalen = ntoh64(hdr + 2);
      break;
    case 0xFE: // 16-bit payload length with masking
      datalen = ntoh16(hdr + 2);
      memcpy(mask, hdr + 4, 4);
      break;
    case 0xFF: // 64-bit payload length with masking
      datalen = ntoh64(hdr + 2);
      memcpy(mask, hdr + 10, 4);
      break;
    default: // 7-bit payload length
      datalen = hdr[1] & 0x7F;
      if (hdr[1] & 0x80) memcpy(mask, hdr + 2, 4);
  }
  if (available < hdrlen + datalen) return CONTINUE;

  // Unmask payload.
  uint8 *data = reinterpret_cast<uint8 *>(req->begin() + hdrlen);
  if (hdr[1] & 0x80) {
    for (int i = 0; i < datalen; ++i) data[i] ^= mask[i % 4];
  }

  // Call handlers for different frame types.
  switch (hdr[0] & 0x0F) {
    case WS_OP_TEXT:
      Receive(data, datalen, false);
      break;

    case WS_OP_BIN:
      Receive(data, datalen, true);
      break;

    case WS_OP_CLOSE:
      Close();
      return CLOSE;

    case WS_OP_PING:
      Ping(data, datalen);
      break;

    case WS_OP_PONG:
      // Ignore PONG frames.
      break;

    default:
      LOG(ERROR) << "unknown ws opcode: " << (hdr[0] & 0x0F);
      return TERMINATE;
  }

  req->Consume(hdrlen + datalen);
  return RESPOND;
}

void WebSocket::Ping(const void *data, size_t size) {
  Send(WS_OP_PONG, data, size);
}

void WebSocket::Close() {
  Send(WS_OP_CLOSE, nullptr, 0);
}

void WebSocket::Send(int type, const void *data, size_t size) {
  uint8 hdr[16];

  // Setup fragment header.
  int hdrlen = 2;
  hdr[0] = type | 0x80;
  if (size < 0x7E) {
    hdr[1] = size;
  } else if (size < 0x10000) {
    hdr[1] = 0x7E;
    hdr[2] = size >> 8;
    hdr[3] = size >> 0;
    hdrlen = 4;
  } else {
    hdr[1] = 0x7F;
    hdr[2] = size >> 56;
    hdr[3] = size >> 48;
    hdr[4] = size >> 40;
    hdr[5] = size >> 32;
    hdr[6] = size >> 24;
    hdr[7] = size >> 16;
    hdr[8] = size >> 8;
    hdr[9] = size >> 0;
    hdrlen = 10;
  }

  // Write header and payload to output.
  conn_->Push(&hdr, hdrlen, data, size);
}

}  // namespace sling


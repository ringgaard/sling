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

#ifndef SLING_NET_SOCKET_SERVER_H_
#define SLING_NET_SOCKET_SERVER_H_

#include <time.h>
#include <atomic>
#include <vector>

#include "sling/base/buffer.h"
#include "sling/base/status.h"
#include "sling/base/types.h"
#include "sling/file/file.h"
#include "sling/util/mutex.h"
#include "sling/util/thread.h"

namespace sling {

// Forward declarations.
class SocketConnection;
class SocketProtocol;
class SocketSession;

// Socket connection states.
enum SocketState {
  SOCKET_STATE_IDLE,
  SOCKET_STATE_RECEIVE,
  SOCKET_STATE_PROCESS,
  SOCKET_STATE_SEND,
  SOCKET_STATE_TERMINATE,
};

// Socket server configuration.
struct SocketServerOptions {
  // Number of worker threads.
  int num_workers = 5;

  // Maximum number of worker threads.
  int max_workers = 200;

  // Number of events per worker poll.
  int max_events = 1;

  // Timeout (in milliseconds) for event polling.
  int timeout = 2000;

  // Maximum idle time (in seconds) before connection is shut down.
  int max_idle = 600;

  // Initial buffer size.
  int initial_bufsiz = 1 << 10;

  // File data buffer size.
  int file_bufsiz = 1 << 16;
};

// Socket server.
class SocketServer {
 public:
  SocketServer(const SocketServerOptions &options) : options_(options) {}
  ~SocketServer();

  // Add listener for protocol on port.
  void Listen(int port, SocketProtocol *protocol);

  // Start socket server listening on the port.
  Status Start();

  // Wait for shutdown.
  void Wait();

  // Shut down socket server.
  void Shutdown();

  // Configuration options.
  const SocketServerOptions &options() const { return options_; }

  // Output connection information as HTML.
  void OutputSocketZ(Buffer *out) const;

 private:
  // Endpoint for listening for new connections for protocol.
  struct Endpoint {
    Endpoint(int port, SocketProtocol *protocol)
        : port(port), protocol(protocol) {}

    int port;                  // port for listening for new connections.
    SocketProtocol *protocol;  // protocol handler for endpoint
    int sock = -1;             // listen socket
    uint64 num_connects = 0;   // number of connections accepted
    Endpoint *next;            // next endpoint
  };

  // Worker handler.
  void Worker();

  // Accept new connection.
  void AcceptConnection(Endpoint *ep);

  // Process I/O events for connection.
  void Process(SocketConnection *conn, int events);

  // Add connection to server.
  void AddConnection(SocketConnection *conn);

  // Remove connection from server.
  void RemoveConnection(SocketConnection *conn);

  // Shut down idle connections.
  void ShutdownIdleConnections();

  // Server configuration.
  SocketServerOptions options_;

  // File descriptor for epoll.
  int pollfd_ = -1;

  // Mutex for serializing access to server state.
  mutable Mutex mu_;

  // List of listening endpoints.
  Endpoint *endpoints_ = nullptr;

  // List of active connections.
  SocketConnection *connections_ = nullptr;

  // Worker threads.
  WorkerPool workers_;

  // Number of active worker threads.
  std::atomic<int> active_{0};

  // Number of idle worker threads.
  std::atomic<int> idle_{0};

  // Flag to determine if server is shutting down.
  bool stop_ = false;
};

// Socket connection.
class SocketConnection {
 public:
  // Initialize new socket connection on socket.
  SocketConnection(SocketServer *server, int sock, SocketProtocol *protocol);
  ~SocketConnection();

  // Process I/O for connection.
  Status Process();

  // Upgrade to another protocol. This should only be called in the Process()
  // method of the current session. The current session will be deleted when
  // the Process() method completes.
  void Upgrade(SocketSession *session);

  // Set file for streaming response. This will take ownership of the file.
  void SendFile(File *file) { file_ = file; }

  // Server for connection.
  SocketServer *server() const { return server_; }

  // I/O buffers.
  Buffer *request() { return &request_; }
  Buffer *response_header() { return &response_header_; }
  Buffer *response_body() { return &response_body_; }

  // Return connection session.
  SocketSession *session() { return session_; }

  // Return connection state name.
  const char *State() const;

 private:
  // Receive data into buffer until it is full or all data that can be received
  // without blocking has been received.
  Status Recv(Buffer *buffer, bool *done);

  // Send data from buffer until all data has been sent or all the data that can
  // be sent without blocking has been sent.
  Status Send(Buffer *buffer, bool *done);

  // Shut down connection.
  void Shutdown();

  // Server for connection.
  SocketServer *server_;

  // Connection session.
  SocketSession *session_;

  // Socket for connection.
  int sock_;

  // Connection state.
  SocketState state_;

  // Last time event was received on connection.
  time_t last_;

  // Connection list.
  SocketConnection *next_;
  SocketConnection *prev_;

  // Buffers for request/response header/body.
  Buffer request_;
  Buffer response_header_;
  Buffer response_body_;

  // File for streaming response.
  File *file_ = nullptr;

  // Close connection after response has been sent.
  bool close_ = false;

  // Statistics.
  uint64 rx_bytes_ = 0;
  uint64 tx_bytes_ = 0;
  uint64 num_requests_ = 0;

  // Mutex for serializing access to connection state.
  Mutex mu_;

  friend class SocketServer;
};

// A protocol is listening on a port and is used for creating new sessions for
// the protocol.
class SocketProtocol {
 public:
  virtual ~SocketProtocol() = default;

  // Return protocol name.
  virtual const char *Name() = 0;

  // Create new session for protocol.
  virtual SocketSession *NewSession(SocketConnection *conn) = 0;
};

// A session object handles one connection for a protocol.
class SocketSession {
 public:
  enum Continuation {
    CONTINUE,    // keep receiving data for request
    RESPOND,     // send back response and flush request
    UPGRADE,     // send back response and switch to new protocol session
    CLOSE,       // send back response and close connection
    TERMINATE,   // terminate session
  };

  virtual ~SocketSession() = default;

  // Return protocol name for session.
  virtual const char *Name() = 0;

  // Process the request in the request buffer and return the response header
  // and body. Return false to terminate the session.
  virtual Continuation Process(SocketConnection *conn) = 0;
};

}  // namespace sling

#endif  // SLING_NET_SOCKET_SERVER_H_


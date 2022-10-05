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
#include <netinet/in.h>

#include "sling/base/status.h"
#include "sling/base/types.h"
#include "sling/file/file.h"
#include "sling/util/iobuffer.h"
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
  SOCKET_STATE_SHUTDOWN,
};

// Socket server configuration.
struct SocketServerOptions {
  // Number of worker threads.
  int num_workers = 16;

  // Number of events per worker poll.
  int max_events = 1;

  // Timeout (in milliseconds) for event polling.
  int timeout = 1000;

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

  // Add listener for protocol on port. If addr is null, it will listen on all
  // interfaces.
  void Listen(const char *addr, int port, SocketProtocol *protocol);

  // Start socket server listening on the port.
  Status Start();

  // Wait for shutdown.
  void Wait();

  // Shut down socket server.
  void Shutdown();

  // Configuration options.
  const SocketServerOptions &options() const { return options_; }

  // Output connection information as HTML.
  void OutputSocketZ(IOBuffer *out) const;

  // Check if server has been started.
  bool started() const { return pollfd_ != -1; }

 private:
  // Endpoint for listening for new connections for protocol.
  struct Endpoint {
    Endpoint(const char *addr, int port, SocketProtocol *protocol);

    struct sockaddr_in sin;    // port and address for listening
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

  // Find endpoint. Return null if endpoint is not known.
  Endpoint *FindEndpoint(void *ep);

  // Lock connection. Return null if connection is not known.
  SocketConnection *LockConnection(void *conn);

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

  // Process I/O for connection.
  Status Process();

  // Upgrade to another protocol. This should only be called in the Process()
  // method of the current session. The current session will be deleted when
  // the Process() method completes.
  void Upgrade(SocketSession *session);

  // Set file for streaming response. This will take ownership of the file.
  void SendFile(File *file) { file_ = file; }

  // Send data back to client.
  void Push(const void *hdr, size_t hdrlen, const void *data, size_t datalen);

  // Server for connection.
  SocketServer *server() const { return server_; }

  // I/O buffers.
  IOBuffer *request() { return &request_; }
  const IOBuffer *request() const { return &request_; }
  IOBuffer *response_header() { return &response_header_; }
  IOBuffer *response_body() { return &response_body_; }

  // Return connection session.
  SocketSession *session() { return session_; }

  // Return connection state name.
  const char *State() const;

  // Lock/unlock access to connection.
  void Lock();
  void Unlock();

  // Last time event was received on connection.
  time_t last() const { return last_; }

 private:
  // Receive data into buffer until it is full or all data that can be received
  // without blocking has been received.
  Status Recv(IOBuffer *buffer, bool *done);

  // Send data from buffer until all data has been sent or all the data that can
  // be sent without blocking has been sent.
  Status Send(IOBuffer *buffer, bool *done);

  // Reference counting.
  void AddRef() const { refs_.fetch_add(1); };
  void Release() const { if (refs_.fetch_sub(1) == 1) delete this; }
  ~SocketConnection();

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

  // Idle timeout in seconds.
  int idle_timeout_;

  // Connection list.
  SocketConnection *next_;
  SocketConnection *prev_;

  // Buffers for request and response header/body.
  IOBuffer request_;
  IOBuffer response_header_;
  IOBuffer response_body_;

  // File for streaming response.
  File *file_ = nullptr;

  // Close connection after response has been sent.
  bool close_ = false;

  // Thread handle for worker processing a request on the connection.
  pthread_t worker_ = 0;

  // Statistics.
  uint64 rx_bytes_ = 0;
  uint64 tx_bytes_ = 0;
  uint64 num_requests_ = 0;

  // Mutex for serializing access to connection state.
  Mutex mu_;

  // Reference count.
  mutable std::atomic<int> refs_{1};

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

  // Return user agent for session.
  virtual const char *Agent() { return ""; }

  // Return idle timeout in seconds for session.
  virtual int IdleTimeout() { return -1; }

  // Lock/unlock session.
  virtual void Lock() {}
  virtual void Unlock() {}

  // Process the request in the request buffer and return the response header
  // and body. Return false to terminate the session.
  virtual Continuation Process(SocketConnection *conn) = 0;
};

}  // namespace sling

#endif  // SLING_NET_SOCKET_SERVER_H_


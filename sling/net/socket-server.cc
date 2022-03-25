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

#include "sling/net/socket-server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string>

#include "sling/base/logging.h"
#include "sling/util/json.h"

namespace sling {

// Return system error.
static Status Error(const char *context) {
  return Status(errno, context, strerror(errno));
}

SocketServer::Endpoint::Endpoint(const char *addr,
                                 int port,
                                 SocketProtocol *protocol)
    : protocol(protocol) {
  sin.sin_family = AF_INET;
  if (addr && *addr) {
    inet_aton(addr, &sin.sin_addr);
  } else {
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
  }
  sin.sin_port = htons(port);
}

SocketServer::~SocketServer() {
  // Close poll descriptor.
  VLOG(1) << "Stop event polling";
  if (pollfd_ != -1) close(pollfd_);

  // Delete listeners.
  VLOG(1) << "Stop listeners";
  Endpoint *endpoint = endpoints_;
  while (endpoint != nullptr) {
    Endpoint *next = endpoint->next;
    if (endpoint->sock != -1) close(endpoint->sock);
    delete endpoint;
    endpoint = next;
  }

  // Delete connections.
  VLOG(1) << "Close connections";
  SocketConnection *conn = connections_;
  while (conn != nullptr) {
    SocketConnection *next = conn->next_;
    delete conn;
    conn = next;
  }
  VLOG(1) << "Socket server shut down";
}

void SocketServer::Listen(const char *addr, int port,
                          SocketProtocol *protocol) {
  Endpoint *endpoint = new Endpoint(addr, port, protocol);
  endpoint->next = endpoints_;
  endpoints_ = endpoint;
}

Status SocketServer::Start() {
  int rc;

  // Create poll file descriptor.
  pollfd_ = epoll_create(1);
  if (pollfd_ < 0) return Error("epoll_create");

  // Create listen sockets.
  for (Endpoint *ep = endpoints_; ep != nullptr; ep = ep->next) {
    ep->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ep->sock < 0) return Error("socket");
    rc = fcntl(ep->sock, F_SETFL, O_NONBLOCK);
    if (rc < 0) return Error("fcntl");
    int on = 1;
    rc = setsockopt(ep->sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if (rc < 0) return Error("setsockopt");

    // Bind listen socket.
    rc = bind(ep->sock, reinterpret_cast<struct sockaddr *>(&ep->sin),
              sizeof(ep->sin));
    if (rc < 0) return Error("bind");

    // Start listening on socket.
    rc = listen(ep->sock, SOMAXCONN);
    if (rc < 0) return Error("listen");

    // Add listening socket to poll descriptor.
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = ep;
    rc = epoll_ctl(pollfd_, EPOLL_CTL_ADD, ep->sock, &ev);
    if (rc < 0) return Error("epoll_ctl");
  }

  // Start workers.
  workers_.Start(options_.num_workers, [this](int index) { this->Worker(); });

  return Status::OK;
}

void SocketServer::Worker() {
  // Allocate event structure.
  int max_events = options_.max_events;
  struct epoll_event *events = new epoll_event[max_events];

  // Keep processing events until server is shut down.
  while (!stop_) {
    // Get new events.
    idle_++;
    int rc = epoll_wait(pollfd_, events, max_events, options_.timeout);
    idle_--;
    if (stop_) break;
    if (rc < 0) {
      if (errno == EINTR) continue;
      LOG(ERROR) << Error("epoll_wait");
      break;
    }
    if (rc == 0) {
      ShutdownIdleConnections();
      continue;
    }

    // Start new worker if all workers are busy.
    if (++active_ == workers_.size() && options_.max_workers > 0) {
      MutexLock lock(&mu_);
      if (workers_.size() < options_.max_workers) {
        VLOG(3) << "Starting new worker thread " << workers_.size();
        workers_.Start(1, [this](int index) { this->Worker(); });
      } else {
        LOG(WARNING) << "All socket worker threads are busy";
      }
    }

    // Process events.
    for (int i = 0; i < rc; ++i) {
      struct epoll_event *ev = &events[i];

      // Check for new connection.
      Endpoint *ep = endpoints_;
      while (ep != nullptr) {
        if (ep == ev->data.ptr) break;
        ep = ep->next;
      }
      if (ep != nullptr) {
        // New connection.
        AcceptConnection(ep);
      } else {
        // Check if connection has been closed.
        auto *conn = reinterpret_cast<SocketConnection *>(ev->data.ptr);
        if (ev->events & (EPOLLHUP | EPOLLERR)) {
          // Detach socket from poll descriptor.
          if (ev->events & EPOLLERR) {
            VLOG(5) << "Error polling socket " << conn->sock_;
          }
          rc = epoll_ctl(pollfd_, EPOLL_CTL_DEL, conn->sock_, ev);
          if (rc < 0) {
            VLOG(2) << Error("epoll_ctl");
          } else {
            // Delete client connection.
            VLOG(3) << "Close socket " << conn->sock_;
            RemoveConnection(conn);
            delete conn;
          }
        } else {
          // Process connection data.
          VLOG(5) << "Begin " << conn->sock_ << " in state " << conn->State();
          do {
            Status s = conn->Process();
            if (!s.ok()) {
              LOG(ERROR) << "Socket error: " << s;
              conn->state_ = SOCKET_STATE_TERMINATE;
            }
            if (conn->state_ == SOCKET_STATE_IDLE) {
              VLOG(5) << "Process " << conn->sock_ << " again";
            }
          } while (conn->state_ == SOCKET_STATE_IDLE);
          VLOG(5) << "End " << conn->sock_ << " in state " << conn->State();

          if (conn->state_ == SOCKET_STATE_TERMINATE) {
            conn->Shutdown();
            VLOG(5) << "Shutdown connection";
          } else {
            conn->last_ = time(0);
          }
        }
      }
    }
    active_--;
  }

  // Free event structure.
  delete [] events;
}

void SocketServer::Wait() {
  // Wait until all workers have terminated.
  workers_.Join();
}

void SocketServer::Shutdown() {
  // Set stop flag to terminate worker threads.
  stop_ = true;
}

void SocketServer::AcceptConnection(Endpoint *ep) {
  int rc;

  // Accept new connection from listen socket.
  struct sockaddr_in addr;
  socklen_t len = sizeof(addr);
  int sock = accept(ep->sock, reinterpret_cast<struct sockaddr *>(&addr), &len);
  if (sock < 0) {
    if (errno != EAGAIN) LOG(WARNING) << Error("accept");
    return;
  }

  // Set non-blocking mode for socket.
  int flags = fcntl(sock, F_GETFL, 0);
  rc = fcntl(sock, F_SETFL, flags | O_NONBLOCK);
  if (rc < 0) LOG(WARNING) << Error("fcntl");

  // Disable Nagle's algorithm.
  int val = 1;
  rc = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(int));
  if (rc < 0) LOG(WARNING) << Error("setsockopt(TCP_NODELAY)");

  // Create new connection.
  VLOG(3) << "New socket connection " << sock;
  SocketConnection *conn = new SocketConnection(this, sock, ep->protocol);
  AddConnection(conn);

  // Add new connection to poll descriptor.
  struct epoll_event ev;
  ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
  ev.data.ptr = conn;
  rc = epoll_ctl(pollfd_, EPOLL_CTL_ADD, sock, &ev);
  if (rc < 0) LOG(WARNING) << Error("epoll_ctl");
  ep->num_connects++;
}

void SocketServer::AddConnection(SocketConnection *conn) {
  MutexLock lock(&mu_);
  conn->next_ = connections_;
  conn->prev_ = nullptr;
  if (connections_ != nullptr) connections_->prev_ = conn;
  connections_ = conn;
}

void SocketServer::RemoveConnection(SocketConnection *conn) {
  MutexLock lock(&mu_);
  if (conn->prev_ != nullptr) conn->prev_->next_ = conn->next_;
  if (conn->next_ != nullptr) conn->next_->prev_ = conn->prev_;
  if (conn == connections_) connections_ = conn->next_;
  conn->next_ = conn->prev_ = nullptr;
}

void SocketServer::ShutdownIdleConnections() {
  MutexLock lock(&mu_);
  time_t now = time(0);
  SocketConnection *conn = connections_;
  while (conn != nullptr) {
    if (now - conn->last_ > conn->idle_timeout_) {
      conn->Shutdown();
      VLOG(5) << "Shut down idle connection";
    }
    conn = conn->next_;
  }
}

void SocketServer::OutputSocketZ(IOBuffer *out) const {
  MutexLock lock(&mu_);

  JSON::Object json;
  time_t now = time(0);
  json.Add("workers", workers_.size());
  json.Add("active", active_);
  json.Add("idle", idle_);

  // Connections.
  JSON::Array *conns = json.AddArray("connections");
  for (auto *conn = connections_; conn != nullptr; conn = conn->next_) {
    JSON::Object *conninfo = conns->AddObject();
    conninfo->Add("socket", conn->sock_);
    conninfo->Add("protocol", conn->session()->Name());

    // Client address.
    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);
    struct sockaddr *saddr = reinterpret_cast<sockaddr *>(&peer);
    if (getpeername(conn->sock_, saddr, &plen) != -1) {
      conninfo->Add("client_address", inet_ntoa(peer.sin_addr));
      conninfo->Add("client_port", ntohs(peer.sin_port));
    }

    // Received, transmitted, and number of requests.
    conninfo->Add("rx_bytes", conn->rx_bytes_);
    conninfo->Add("tx_bytes", conn->tx_bytes_);
    conninfo->Add("requests", conn->num_requests_);

    // Socket state.
    int err = 0;
    socklen_t errlen = sizeof(err);
    int rc  = getsockopt(conn->sock_, SOL_SOCKET, SO_ERROR, &err, &errlen);
    const char *error = "OK";
    if (rc != 0) {
      error = strerror(rc);
    } else if (err != 0) {
      error = strerror(err);
    }
    conninfo->Add("status", error);

    // Connection state.
    conninfo->Add("state", conn->State());

    // Idle time.
    conninfo->Add("idle", now - conn->last_);

    // User agent.
    conninfo->Add("agent", conn->session()->Agent());
  }

  // Endpoints.
  JSON::Array *endpoints = json.AddArray("endpoints");
  for (auto *ep = endpoints_; ep != nullptr; ep = ep->next) {
    JSON::Object *epinfo = endpoints->AddObject();

    // Port.
    int port = ntohs(ep->sin.sin_port);
    epinfo->Add("port", port);

    // Socket.
    epinfo->Add("socket", ep->sock);

    // Protocol.
    epinfo->Add("protocol", ep->protocol->Name());

    // Connects.
    epinfo->Add("connects", ep->num_connects);
  }

  json.Write(out);
}

SocketConnection::SocketConnection(SocketServer *server, int sock,
                                   SocketProtocol *protocol)
    : server_(server), sock_(sock) {
  next_ = prev_ = nullptr;
  state_ = SOCKET_STATE_IDLE;
  session_ = protocol->NewSession(this);
  last_ = time(0);
  idle_timeout_ = session_->IdleTimeout();
  if (idle_timeout_ == -1) idle_timeout_ = server->options().max_idle;
}

SocketConnection::~SocketConnection() {
  MutexLock lock(&mu_);

  // Delete session.
  delete session_;

  // Close client connection.
  close(sock_);

  // Close response file.
  if (file_ != nullptr) file_->Close();
}

Status SocketConnection::Process() {
  MutexLock lock(&mu_);
  SocketSession *session = session_;
  switch (state_) {
    case SOCKET_STATE_IDLE:
      // Allocate request buffer.
      if (request_.capacity() == 0) {
        request_.Reset(server_->options().initial_bufsiz);
      }

      // Prepare for receiving request.
      state_ = SOCKET_STATE_RECEIVE;
      FALLTHROUGH_INTENDED;

    case SOCKET_STATE_RECEIVE: {
      // Keep reading until input is exhausted.
      bool done = false;
      size_t before = request_.available();
      while (!done) {
        // Expand request buffer to ensure we have room to read data.
        request_.Ensure(1);

        // Receive more data.
        Status st = Recv(&request_, &done);
        if (!st.ok()) return st;
        if (state_ == SOCKET_STATE_TERMINATE) return Status::OK;
      }

      // Check if any input was received.
      size_t after = request_.available();
      if (after == before) return Status::OK;

      state_ = SOCKET_STATE_PROCESS;
      FALLTHROUGH_INTENDED;
    }

    case SOCKET_STATE_PROCESS:
      // Process received data.
      switch (session_->Process(this)) {
        case SocketSession::CONTINUE:
          state_ = SOCKET_STATE_RECEIVE;
          return Status::OK;
        case SocketSession::RESPOND:
          break;
        case SocketSession::UPGRADE:
          if (session != session_) {
            delete session;
            session = session_;
          }
          break;
        case SocketSession::CLOSE:
          close_ = true;
          break;
        case SocketSession::TERMINATE:
          state_ = SOCKET_STATE_TERMINATE;
          return Status::OK;
      }

      num_requests_++;
      state_ = SOCKET_STATE_SEND;
      FALLTHROUGH_INTENDED;

    case SOCKET_STATE_SEND: {
      // Send response header.
      while (response_header_.available() > 0) {
        bool done;
        Status st = Send(&response_header_, &done);
        if (!st.ok()) return st;
        if (done) return Status::OK;
      }

      // Send response body.
      while (response_body_.available() > 0) {
        bool done;
        Status st = Send(&response_body_, &done);
        if (!st.ok()) return st;
        if (done) return Status::OK;
      }

      // Send file data.
      while (file_ != nullptr) {
        if (response_body_.empty()) {
          // Read next chunk from file.
          uint64 read;
          response_body_.Reset(server_->options().file_bufsiz);
          Status st = file_->Read(response_body_.end(),
                                  response_body_.remaining(),
                                  &read);
          response_body_.Append(read);

          if (!st.ok()) {
            // Error reading file.
            LOG(ERROR) << "File read error: " << st;
            file_->Close();
            file_ = nullptr;
            return st;
          }

          if (read == 0) {
            // End of file.
            file_->Close();
            file_ = nullptr;
          }
        }

        // Send next file chunk.
        while (response_body_.available() > 0) {
          bool done;
          Status st = Send(&response_body_, &done);
          if (!st.ok()) return st;
          if (done) return Status::OK;
        }
      }

      // Reset buffer and go back to idle if connection should be kept open.
      if (!close_) {
        // Clear buffers.
        request_.Flush();
        response_header_.Clear();
        response_body_.Clear();

        // Mark connection as idle.
        state_ = SOCKET_STATE_IDLE;
        return Status::OK;
      }

      state_ = SOCKET_STATE_TERMINATE;
      FALLTHROUGH_INTENDED;
    }

    case SOCKET_STATE_TERMINATE:
      return Status::OK;

    default:
      return Status(1, "Invalid socket state");
  }
}

Status SocketConnection::Recv(IOBuffer *buffer, bool *done) {
  *done = false;
  int rc = recv(sock_, buffer->end(), buffer->remaining(), 0);
  if (rc <= 0) {
    *done = true;
    if (rc == 0) {
      // Connection closed.
      state_ = SOCKET_STATE_TERMINATE;
      return Status::OK;
    } else if (errno == EAGAIN) {
      // No more data available for now.
      VLOG(6) << "Recv " << sock_ << " again";
      return Status::OK;
    } else {
      // Receive error.
      VLOG(6) << "Recv " << sock_ << " error";
      return Error("recv");
    }
  }
  VLOG(6) << "Recv " << sock_ << ", " << rc << " bytes";
  buffer->Append(rc);
  rx_bytes_ += rc;
  return Status::OK;
}

Status SocketConnection::Send(IOBuffer *buffer, bool *done) {
  *done = false;
  int rc  = send(sock_, buffer->begin(), buffer->available(), MSG_NOSIGNAL);
  if (rc <= 0) {
    *done = true;
    if (rc == 0) {
      // Connection closed.
      VLOG(6) << "Send " << sock_ << " closed";
      state_ = SOCKET_STATE_TERMINATE;
      return Status::OK;
    } else if (errno == EAGAIN) {
      // Output queue full.
      VLOG(6) << "Send " << sock_ << " again";
      return Status::OK;
    } else {
      // Send error.
      VLOG(6) << "Send " << sock_ << " done";
      return Error("send");
    }
  }
  VLOG(6) << "Send " << sock_ << ", " << rc << " bytes";
  buffer->Consume(rc);
  tx_bytes_ += rc;
  return Status::OK;
}

void SocketConnection::Upgrade(SocketSession *session) {
  CHECK_EQ(state_, SOCKET_STATE_PROCESS)
      << "Socket protocol upgrade only allowed in PROCESS state";
  session_ = session;
  if (session_->IdleTimeout() != -1) {
    idle_timeout_ = session_->IdleTimeout();
  }
}

void SocketConnection::Shutdown() {
  shutdown(sock_, SHUT_RDWR);
}

void SocketConnection::Push() {
  if (state_ > SOCKET_STATE_RECEIVE) return;
  if (response_body()->empty()) return;
  state_ = SOCKET_STATE_SEND;
  Process();
}

const char *SocketConnection::State() const {
  switch (state_) {
    case SOCKET_STATE_IDLE: return "IDLE";
    case SOCKET_STATE_RECEIVE: return request()->empty() ? "READY" : "RECEIVE";
    case SOCKET_STATE_PROCESS: return "PROCESS";
    case SOCKET_STATE_SEND: return "SEND";
    case SOCKET_STATE_TERMINATE: return "TERMINATE";
  }
  return "???";
}

}  // namespace sling


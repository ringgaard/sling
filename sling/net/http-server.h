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

#ifndef SLING_NET_HTTP_SERVER_H_
#define SLING_NET_HTTP_SERVER_H_

#include  <functional>
#include  <string>
#include  <vector>

#include "sling/net/http-utils.h"
#include "sling/net/socket-server.h"
#include "sling/util/iobuffer.h"

namespace sling {

using namespace std::placeholders;

class HTTPRequest;
class HTTPResponse;

// HTTP protocol handler.
class HTTPProtocol : public SocketProtocol {
 public:
  // HTTP handler.
  typedef std::function<void(HTTPRequest *, HTTPResponse *)> Handler;

  // Initialize HTTP protocol handler.
  HTTPProtocol();

  // Register handler for requests.
  void Register(const string &uri, const Handler &handler);

  // Register method for handling requests.
  template<class T> void Register(
      const string &uri, T *object,
      void (T::*method)(HTTPRequest *, HTTPResponse *)) {
    Register(uri, std::bind(method, object, _1, _2));
  }

  // Socket protocol interface.
  const char *Name() override { return "HTTP"; }
  SocketSession *NewSession(SocketConnection *conn) override;

  // Find handler for request.
  Handler FindHandler(HTTPRequest *request) const;

 private:
  // HTTP context for serving requests under an URI.
  struct Context {
    Context(const string &u, const Handler &h) : uri(u), handler(h) {
      if (uri == "/") uri = "";
    }
    string uri;
    Handler handler;
  };

  // Handler for /helpz.
  void HelpHandler(HTTPRequest *req, HTTPResponse *rsp);

  // Handler for /sockz.
  void SocketHandler(HTTPRequest *req, HTTPResponse *rsp);

  // Handler for /healthz.
  void HealthHandler(HTTPRequest *req, HTTPResponse *rsp);

  // Registered HTTP handlers.
  std::vector<Context> contexts_;

  // Mutex for serializing access to contexts.
  mutable Mutex mu_;
};

// HTTP server with a socket server and an HTTP protocol handler.
class HTTPServer : public SocketServer {
 public:
  typedef HTTPProtocol::Handler Handler;

  HTTPServer(const SocketServerOptions &options, const char *addr, int port)
      : SocketServer(options) {
    Listen(addr, port, &http_);
  }

  // Register handler for requests.
  void Register(const string &uri, const Handler &handler) {
    http_.Register(uri, handler);
  }

  // Register method for handling requests.
  template<class T> void Register(
      const string &uri, T *object,
      void (T::*method)(HTTPRequest *, HTTPResponse *)) {
    http_.Register(uri, object, method);
  }

 private:
  // HTTP protocol handler.
  HTTPProtocol http_;
};

// HTTP session.
class HTTPSession : public SocketSession {
 public:
  // Initialize new HTTP session for connection.
  HTTPSession(HTTPProtocol *http, SocketConnection *conn);

  // Clear HTTP session.
  ~HTTPSession() override;

  // Socket session interface.
  const char *Name() override { return "HTTP"; }
  const char *Agent() override { return agent_ ? agent_ : ""; }
  Continuation Process(SocketConnection *conn) override;

  // Parse header. Returns true when header has been parsed.
  bool ParseHeader();

  // Dispatch request to handler.
  void Dispatch();

  // Return HTTP request information.
  HTTPRequest *request() const { return request_; }

  // Return HTTP response information.
  HTTPResponse *response() const { return response_; }

  // Append data to response.
  void AppendResponse(const char *data, int size) {
    conn_->response_body()->Write(data, size);
  }

  // Set file for streaming response. This will take ownership of the file.
  void SendFile(File *file) { conn_->SendFile(file); }

  // Request and response body buffers.
  IOBuffer *request_buffer() { return conn_->request(); }
  IOBuffer *response_buffer() { return conn_->response_body(); }

  // Return session connection.
  SocketConnection *conn() const { return conn_; }

  // Upgrade protocol.
  void Upgrade(SocketSession *session) {
    conn_->Upgrade(session);
    action_ = UPGRADE;
  }

 private:
  // HTTP protocol handler.
  HTTPProtocol *http_;

  // Socket connection for session.
  SocketConnection *conn_;

  // Current HTTP request for connection.
  HTTPRequest *request_ = nullptr;

  // Current HTTP response for connection.
  HTTPResponse *response_ = nullptr;

  // HTTP request header buffer.
  IOBuffer request_header_;

  // User agent for session.
  char *agent_ = nullptr;

  // Action taken after request has been processed.
  Continuation action_ = CLOSE;
};

// HTTP request.
class HTTPRequest {
 public:
  // Initialize request form HTTP header.
  HTTPRequest(HTTPSession *session, IOBuffer *hdr);

  // Is this a valid HTTP request?
  bool valid() const { return valid_; }

  // Is this a HTTP/1.1 request?
  bool http11() const { return http11_; }

  // HTTP method.
  const char *method() const { return method_; }
  HTTPMethod Method() const { return GetHTTPMethod(method_); }

  // HTTP URL path.
  const char *full_path() const { return full_path_; }
  const char *path() const { return path_; }
  void set_path(const char *path) { path_ = path; }

  // HTTP URL query.
  const char *query() const { return query_; }

  // HTTP protocol.
  const char *protocol() const { return protocol_; }

  // HTTP content type.
  const char *content_type() const { return content_type_; }

  // HTTP content length.
  int content_length() const { return content_length_; }

  // HTTP keep-alive flag.
  bool keep_alive() const { return keep_alive_; }

  // Get HTTP header.
  const char *Get(const char *name, const char *defval = nullptr) const;
  int64 Get(const char *name, int64 defval) const;

  // HTTP request headers.
  const std::vector<HTTPHeader> &headers() const { return headers_; }

  // HTTP request body content.
  const char *content() const { return content_; }
  int content_size() const { return content_size_; }
  void set_content(const char *content, int size) {
   content_ = content;
   content_size_ = size;
  }

  // Return session connection.
  SocketConnection *conn() const { return session_->conn(); }

 private:
  // HTTP session for request.
  HTTPSession *session_;

  // Is HTTP request valid?
  bool valid_ = false;

  // Is this a HTTP/1.1 request?
  bool http11_ = false;

  // HTTP method.
  const char *method_ = nullptr;

  // HTTP URI full path (including context).
  const char *full_path_ = nullptr;

  // HTTP URI path.
  const char *path_ = nullptr;

  // HTTP URI query.
  const char *query_ = nullptr;

  // HTTP protocol.
  const char *protocol_ = nullptr;

  // Standard HTTP request headers.
  const char *content_type_ = nullptr;
  int content_length_ = 0;
  bool keep_alive_ = false;

  // HTTP request headers.
  std::vector<HTTPHeader> headers_;

  // HTTP request body.
  const char *content_ = nullptr;
  int content_size_ = 0;
};

// HTTP response.
class HTTPResponse {
 public:
  HTTPResponse(HTTPSession *session) : session_(session) {}
  ~HTTPResponse();

  // HTTP status code.
  int status() const { return status_; }
  void set_status(int status) { status_ = status; }

  // HTTP content type.
  const char *content_type() const { return Get("Content-Type"); }
  void set_content_type(const char *type) { Set("Content-Type", type); }

  // HTTP content length.
  int content_length() const { return content_length_; }
  void set_content_length(int length) { content_length_ = length; }

  // Get response header. Returns null if header is not set.
  const char *Get(const char *name, const char *defval = nullptr) const;

  // Set response header.
  void Set(const char *name, const char *value, bool overwrite = true);
  void Set(const char *name, int64 value, bool overwrite = true);

  // Add response header.
  void Add(const char *name, const char *value, int len);
  void Add(const char *name, const char *value) {
    Add(name, value, strlen(value));
  }

  // HTTP response headers.
  const std::vector<HTTPHeader> &headers() const { return headers_; }

  // Append data to response.
  void Append(const char *data, int size) {
    session_->AppendResponse(data, size);
  }
  void Append(const char *str) { if (str) Append(str, strlen(str)); }
  void Append(const string &str) { Append(str.data(), str.size()); }
  void AppendNumber(int64 value);

  // Set file for streaming response. This will take ownership of the file.
  void SendFile(File *file) { session_->SendFile(file); }

  // Return HTTP error message.
  void SendError(int status,
                 const char *title = nullptr,
                 const char *msg = nullptr);

  // Permanent redirect to another URL.
  void RedirectTo(const char *uri);

  // Temporary redirect to another URL.
  void TempRedirectTo(const char *uri);

  // Upgrade protocol.
  void Upgrade(SocketSession *session) { session_->Upgrade(session); }

  // Write HTTP header to buffer.
  void WriteHeader(IOBuffer *rsp);

  // HTTP response body buffer.
  IOBuffer *buffer() { return session_->response_buffer(); }

 private:
  // HTTP session for request.
  HTTPSession *session_;

  // HTTP status code for response.
  int status_ = 200;

  // Content length for response.
  int content_length_ = 0;

  // HTTP response headers.
  std::vector<HTTPHeader> headers_;
};

}  // namespace sling

#endif  // SLING_NET_HTTP_SERVER_H_


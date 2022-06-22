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

#include "sling/net/http-server.h"

#include "sling/string/numbers.h"

namespace sling {

static const char *HTTP_SERVER_NAME = "HTTPServer/1.0";

// Return text for HTTP status code.
static const char *StatusText(int status) {
  switch (status) {
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Moved";
    case 304: return "Not Modified";
    case 307: return "Temporary Redirect";
    case 400: return "Bad Request";
    case 401: return "Not Authorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    case 505: return "HTTP Version Not Supported";
  }

  return "Internal Error";
}

// Return 404 error.
static void Handle404(HTTPRequest *request, HTTPResponse *response) {
  response->set_content_type("text/html");
  response->set_status(404);
  response->Append("<html><head>\n");
  response->Append("<title>404 Not Found</title>\n");
  response->Append("</head><body>\n");
  response->Append("<h1>Not Found</h1>\n");
  response->Append("<p>The requested URL ");
  response->Append(HTMLEscape(request->path()));
  response->Append(" was not found on this server.</p>\n");
  response->Append("</body></html>\n");
}

// Return 503 error.
static void Handle503(HTTPRequest *request, HTTPResponse *response) {
  response->set_content_type("text/html");
  response->set_status(503);
  response->Append("<html><head>\n");
  response->Append("<title>404 Service Unavailable</title>\n");
  response->Append("</head><body>\n");
  response->Append("<h1>Service Unavailable</h1>\n");
  response->Append("<p>The system is down for maintenance</p>");
  response->Append("</body></html>\n");
}

// Read HTTP header line.
static char *ReadLine(IOBuffer *hdr) {
  char *line = hdr->begin();
  char *end = hdr->end();
  char *s = line;
  while (s < end) {
    switch (*s) {
      case '\n':
        if (s + 1 < end && (s[1] == ' ' || s[1] == '\t')) {
          // Replace HTTP header continuation with space.
          *s++ = ' ';
        } else {
          //  End of line found. Strip trailing whitespace.
          *s = 0;
          hdr->Consume(s - line + 1);
          while (s > line) {
            s--;
            if (*s != ' ' && *s != '\t') break;
            *s = 0;
          }
          return line;
        }
        break;

      case '\r':
      case '\t':
        // Replace whitespace with space.
        *s++ = ' ';
        break;

      default:
        s++;
    }
  }

  return nullptr;
}

HTTPProtocol::HTTPProtocol() {
  // Register standard handlers.
  Register("/helpz", this, &HTTPProtocol::HelpHandler);
  Register("/sockz", this, &HTTPProtocol::SocketHandler);
  Register("/healthz", this, &HTTPProtocol::HealthHandler);
}

void HTTPProtocol::Register(const string &uri, const Handler &handler) {
  MutexLock lock(&mu_);
  contexts_.emplace_back(uri, handler);
}

SocketSession *HTTPProtocol::NewSession(SocketConnection *conn) {
  return new HTTPSession(this, conn);
}

HTTPProtocol::Handler HTTPProtocol::FindHandler(HTTPRequest *request) const {
  MutexLock lock(&mu_);

  // Return 503 if service not available.
  if (!available_) return &Handle503;

  // Find context with longest matching prefix.
  const char *path = request->path();
  int longest = -1;
  const Context *match = nullptr;
  for (const Context &c : contexts_) {
    int n = c.uri.size();
    const char *s = path + n;
    if (strncmp(c.uri.data(), path, n) == 0 && (*s == '/' || *s == 0)) {
      if (n > longest) {
        match = &c;
        longest = n;
      }
    }
  }

  if (longest >= 0) {
    // Remove matching URI prefix from path.
    request->set_path(path + longest);

    // Return handler.
    return match->handler;
  } else {
    // No match found. Return 404 handler.
    return &Handle404;
  }
}

void HTTPProtocol::HelpHandler(HTTPRequest *req, HTTPResponse *rsp) {
  MutexLock lock(&mu_);
  rsp->set_content_type("text/html");
  rsp->set_status(200);
  rsp->Append("<html><head><title>helpz</title></head><body>\n");
  rsp->Append("Contexts:<ul>\n");
  for (const Context &c : contexts_) {
    if (c.uri.empty()) {
    rsp->Append("<li><a href=\"/\">/</a></li>\n");
    } else {
      rsp->Append("<li><a href=\"");
      rsp->Append(c.uri);
      rsp->Append("\">");
      rsp->Append(c.uri);
      rsp->Append("</a></li>\n");
    }
  }
  rsp->Append("</ul>\n");
  rsp->Append("</body></html>\n");
}

void HTTPProtocol::SocketHandler(HTTPRequest *req, HTTPResponse *rsp) {
  req->conn()->server()->OutputSocketZ(rsp->buffer());
  rsp->set_content_type("text/json");
  rsp->set_status(200);
}


void HTTPProtocol::HealthHandler(HTTPRequest *req, HTTPResponse *rsp) {
  rsp->set_content_type("text/plain");
  rsp->set_status(200);
  rsp->Append("OK");
}

HTTPSession::HTTPSession(HTTPProtocol *http, SocketConnection *conn)
    : http_(http), conn_(conn) {
}

HTTPSession::~HTTPSession() {
  free(agent_);
  delete request_;
  delete response_;
}

SocketSession::Continuation HTTPSession::Process(SocketConnection *conn) {
  // Check if we have received a complete HTTP header.
  if (request_ == nullptr) {
    // Find end of HTTP header.
    Text data(conn->request()->data());
    int eoh = data.find("\r\n\r\n");
    if (eoh == -1) return CONTINUE;
    int hdrlen = eoh + 4;

    // Copy HTTP header to a separate buffer to ensure pointer stability for
    // the header fields.
    request_header_.Write(conn->request()->Consume(hdrlen), hdrlen);

    // Create HTTP request from header.
    delete request_;
    request_ = new HTTPRequest(this, &request_header_);
    if (!request_->valid()) return TERMINATE;

    // Get user agent if not already set.
    if (agent_ == nullptr) {
      const char *ua = request_->Get("User-Agent");
      if (ua) agent_ = strdup(ua);
    }
  }

  // Check if request body has been received.
  if (conn->request()->available() < request_->content_length()) {
    return CONTINUE;
  } else {
    // Set request body content.
    int len = request_->content_length();
    request_->set_content(conn->request()->Consume(len), len);
  }

  // Allocate response object.
  delete response_;
  response_ = new HTTPResponse(this);

  // Dispatch request to handler.
  Dispatch();

  // HEAD requests are not allowed to have a response body.
  if (strcmp(request_->method(), "HEAD") == 0) {
    if (response_buffer()->available() > 0) {
      LOG(WARNING) << "HEAD response body must be empty";
      response_buffer()->Clear();
    }
  }

  // The request and response objects are no longer needed.
  delete request_;
  request_ = nullptr;
  delete response_;
  response_ = nullptr;
  request_header_.Clear();

  // Return action to take after request has completed.
  Continuation action = action_;
  action_ = CLOSE;
  return action;
}

void HTTPSession::Dispatch() {
  // Find handler for request.
  HTTPProtocol::Handler handler = http_->FindHandler(request_);

  // Dispatch request to handler.
  handler(request_, response_);

  // Use response body size as content length if it has not been set.
  if (response_->content_length() == 0 && !response_buffer()->empty()) {
    response_->set_content_length(response_buffer()->available());
  }

  // Add Date: and Server: headers.
  char datebuf[RFCTIME_SIZE];
  response_->Set("Server", HTTP_SERVER_NAME, false);
  response_->Set("Date", RFCTime(time(nullptr), datebuf), false);
  response_->Set("Content-Length", response_->content_length());

  // Return status code 204 (No Content) if response body is empty.
  if (response_->status() == 200 && response_->content_length() == 0) {
    response_->set_status(204);
  }

  // Check for persistent connection.
  if (request_->http11()) {
    action_ = RESPOND;
  } else if (request_->keep_alive()) {
    action_ = RESPOND;
    response_->Set("Connection", "keep-alive");
  }

  // Generate response header buffer.
  response_->WriteHeader(conn_->response_header());
}

HTTPRequest::HTTPRequest(HTTPSession *session, IOBuffer *hdr)
    : session_(session) {
  // Get HTTP line.
  char *s = ReadLine(hdr);
  if (!s) return;

  // Parse method.
  method_ = s;
  s = strchr(s, ' ');
  if (!s) return;
  *s++ = 0;

  // Parse URL path.
  if (*s) {
    full_path_ = path_ = s;

    // Parse URL query.
    char *q = strchr(s, '?');
    if (q) {
      *q++ = 0;
      query_ = q;
      s = q;
    }
  }

  // Parse protocol version.
  if (*s) {
    char *p = strchr(s, ' ');
    if (p) {
      *p++ = 0;
      while (*p == ' ') p++;
      if (*p) {
        protocol_ = p;
      }
    }
  }

  if (protocol_ != nullptr && strcmp(protocol_, "HTTP/1.1") == 0) {
    http11_ = true;
    keep_alive_ = true;
  }

  VLOG(2) << "HTTP method: " << method_ << ", path: " << path_
          << ", query: " << query_ << ", protocol: " << protocol_;

  // Parse headers.
  char *l;
  while ((l = ReadLine(hdr)) != nullptr) {
    // Split header line into key and value.
    if (!*l) continue;
    s = strchr(l, ':');
    if (!s) continue;
    *s++ = 0;
    while (*s == ' ') s++;
    if (!*s) continue;

    // Get standard HTTP headers.
    if (strcasecmp(l, "Content-Type") == 0) {
      content_type_ = s;
    } else if (strcasecmp(l, "Content-Length") == 0) {
      content_length_ = atoi(s);
    } else if (strcasecmp(l, "Connection") == 0) {
      keep_alive_ = strcasecmp(s, "keep-alive") == 0;
    }

    VLOG(4) << "HTTP request header: " << l << ": " << s;
    headers_.emplace_back(l, s);
  }

  // HTTP header successfully parsed.
  valid_ = true;
}

const char *HTTPRequest::Get(const char *name, const char *defval) const {
  for (const HTTPHeader &h : headers_) {
    if (strcasecmp(name, h.name) == 0) return h.value;
  }
  return defval;
}

int64 HTTPRequest::Get(const char *name, int64 defval) const {
  const char *value = Get(name);
  if (value == nullptr) return defval;
  char *ptr = nullptr;
  int64 num = strtoll(value, &ptr, 10);
  if (ptr != value + strlen(value)) return defval;
  return num;
}

HTTPResponse::~HTTPResponse() {
  for (HTTPHeader &h : headers_) {
    free(h.name);
    free(h.value);
  }
}

const char *HTTPResponse::Get(const char *name, const char *defval) const {
  for (const HTTPHeader &h : headers_) {
    if (strcasecmp(name, h.name) == 0) return h.value;
  }
  return defval;
}

void HTTPResponse::Set(const char *name, const char *value, bool overwrite) {
  for (HTTPHeader &h : headers_) {
    if (strcasecmp(name, h.name) == 0) {
      if (overwrite) {
        free(h.value);
        h.value = strdup(value);
      }
      return;
    }
  }
  headers_.emplace_back(strdup(name), strdup(value));
}

void HTTPResponse::Set(const char *name, int64 value, bool overwrite) {
  char buffer[kFastToBufferSize];
  char *num = FastInt64ToBuffer(value, buffer);
  Set(name, num, overwrite);
}

void HTTPResponse::Add(const char *name, const char *value, int len) {
  char *v = static_cast<char *>(malloc(len + 1));
  memcpy(v, value, len);
  v[len] = 0;
  headers_.emplace_back(strdup(name), v);
}

void HTTPResponse::AppendNumber(int64 value) {
  char buffer[kFastToBufferSize];
  char *num = FastInt64ToBuffer(value, buffer);
  Append(num);
}

void HTTPResponse::SendError(int status, const char *title, const char *msg) {
  if (title == nullptr) title = StatusText(status);

  set_content_type("text/html");
  set_status(status);

  buffer()->Clear();
  Append("<html><head>\n");
  Append("<title>");
  if (title != nullptr) {
    AppendNumber(status);
    Append(" ");
    Append(title);
  } else {
    Append("Error ");
    AppendNumber(status);
  }
  Append("</title>\n");
  Append("</head><body>\n");
  if (msg != nullptr) {
    Append(msg);
  } else {
    Append("<p>Error ");
    AppendNumber(status);
    if (title != nullptr) {
      Append(": ");
      Append(title);
    }
    Append("</p>");
  }
  Append("\n</body></html>\n");
}

void HTTPResponse::RedirectTo(const char *uri) {
  string msg;
  string escaped_uri = HTMLEscape(uri);
  msg.append("<h1>Moved</h1>\n");
  msg.append("<p>This page has moved to <a href=\"");
  msg.append(escaped_uri);
  msg.append("\">");
  msg.append(escaped_uri);
  msg.append("</a>.</p>\n");

  Set("Location", uri);
  SendError(301, "Moved Permanently", msg.c_str());
}

void HTTPResponse::TempRedirectTo(const char *uri) {
  string msg;
  string escaped_uri = HTMLEscape(uri);
  msg.append("<h1>Moved</h1>\n");
  msg.append("<p>This page has moved to <a href=\"");
  msg.append(escaped_uri);
  msg.append("\">");
  msg.append(escaped_uri);
  msg.append("</a>.</p>\n");

  Set("Location", uri);
  SendError(307, "Moved Temporary", msg.c_str());
}

void HTTPResponse::WriteHeader(IOBuffer *rsp) {
  // Output HTTP header line.
  if (session_->request()->http11()) {
    rsp->Write("HTTP/1.1");
  } else {
    rsp->Write("HTTP/1.0");
  }
  rsp->Write(" ");

  char statusnum[16];
  FastInt32ToBufferLeft(status_, statusnum);
  rsp->Write(statusnum);
  rsp->Write(" ");
  rsp->Write(StatusText(status_));
  rsp->Write("\r\n");

  VLOG(4) << "HTTP response: " << status_ << " " << StatusText(status_);

  // Output HTTP headers.
  for (const HTTPHeader &h : headers_) {
    rsp->Write(h.name);
    rsp->Write(": ");
    rsp->Write(h.value);
    rsp->Write("\r\n");
    VLOG(4) << "HTTP response header: " << h.name << ": " << h.value;
  }

  rsp->Write("\r\n");
}

}  // namespace sling


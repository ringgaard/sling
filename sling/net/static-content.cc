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

#include "sling/net/static-content.h"

#include <string.h>
#include <string>

#include "sling/base/flags.h"
#include "sling/base/status.h"
#include "sling/file/file.h"
#include "sling/net/http-server.h"
#include "sling/net/http-utils.h"

// Use internal embedded file system for web content by default.
DEFINE_string(webdir, "/intern", "Base directory for serving web contents");
DEFINE_bool(webcache, true, "Enable caching of web content");

namespace sling {

// Check if path is valid, especially that the path is not a relative path and
// does not contain any parent directory parts (..) that could escape the base
// directory.
static bool IsValidPath(const char *filename) {
  const char *p = filename;
  if (*p != '/' && *p != 0) return false;
  while (*p != 0) {
    if (p[0] == '.' && p[1] == '.') {
      if (p[2] == 0 || p[2] == '/') return false;
    }
    while (*p != 0 && *p != '/') p++;
    while (*p == '/') p++;
  }
  return true;
}

StaticContent::StaticContent(const string &url, const string &path)
    : url_(url) {
  // Use configured directory for web content.
  dir_ = FLAGS_webdir;

  // Default to current directory.
  if (dir_.empty()) dir_ = ".";
  if (url_ == "/") url_ = "";

  // Add path for content.
  if (!path.empty() && path != "/") {
    dir_.push_back('/');
    dir_.append(path);
  }
  VLOG(3) << "Serve url " << url << " from " << dir_;
}

void StaticContent::Register(HTTPServer *http) {
  http->Register(url_, this, &StaticContent::HandleFile);
}

void StaticContent::HandleFile(HTTPRequest *request, HTTPResponse *response) {
  // Only GET and HEAD methods allowed.
  HTTPMethod method = GetHTTPMethod(request->method());
  if (method != HTTP_GET && method != HTTP_HEAD) {
    response->SendError(405, "Method Not Allowed", nullptr);
    return;
  }

  // Get path.
  string path;
  if (!DecodeURLComponent(request->path(), &path)) {
    response->SendError(400, "Bad Request", nullptr);
    return;
  }

  // Check that path is valid.
  if (!IsValidPath(path.c_str())) {
    LOG(WARNING) << "Invalid request path: " << request->path();
    response->SendError(403, "Forbidden", nullptr);
    return;
  }

  // Remove trailing slash from file name.
  string filename = dir_ + path;
  VLOG(5) << "url: " << request->path() << " file: " << filename;
  bool trailing_slash = false;
  if (filename.back() == '/') {
    filename.pop_back();
    trailing_slash = true;
  }

  // Get file information.
  FileStat stat;
  Status st = File::Stat(filename, &stat);
  if (!st.ok()) {
    if (st.code() == EACCES) {
      response->SendError(403, "Forbidden", nullptr);
      return;
    } else if (st.code() == ENOENT) {
      if (index_fallback_) {
        // Fall back to index page for unknown files.
        filename = dir_ + "/index.html";
        st = File::Stat(filename, &stat);
        if (!st.ok()) {
          response->SendError(404, "Index file not Found", nullptr);
          return;
        }
      } else {
        response->SendError(404, "Not Found", nullptr);
        return;
      }
    } else {
      string error = HTMLEscape(st.message());
      response->SendError(500, "Internal Server Error", error.c_str());
      return;
    }
  }

  // Redirect to index page for directory.
  if (stat.is_directory) {
    // Redirect to directory with slash if needed.
    if (!trailing_slash) {
      string dir = url_;
      dir.push_back('/');
      if (strlen(request->path()) > 0) {
        dir.append(request->path());
        dir.push_back('/');
      }
      response->RedirectTo(dir.c_str());
      return;
    }

    // Return index page for directory.
    filename.append("/index.html");
    st = File::Stat(filename, &stat);
    if (!st.ok() || stat.is_directory) {
      response->SendError(403, "Forbidden", "Directory browsing not allowed");
      return;
    }
  } else {
    // Regular files cannot have a trailing slash.
    if (trailing_slash) {
      response->SendError(404, "Not Found", nullptr);
      return;
    }
  }

  // Check if file has changed.
  const char *cached = request->Get("If-modified-since");
  const char *control = request->Get("Cache-Control");
  bool refresh = control != nullptr && strcmp(control, "maxage=0") == 0;
  if (!refresh && cached) {
    if (ParseRFCTime(cached) == stat.mtime) {
      response->set_status(304);
      response->set_content_length(0);
      return;
    }
  }

  // Set content type from file extension.
  const char *mimetype = GetMimeType(GetExtension(filename.c_str()));
  if (mimetype != nullptr) {
    response->set_content_type(mimetype);
  }

  // Do not cache content if requested.
  if (!FLAGS_webcache) {
    response->Set("Cache-Control", "no-cache");
  } else {
    // Set file modified time.
    char datebuf[RFCTIME_SIZE];
    response->Set("Last-Modified", RFCTime(stat.mtime, datebuf));
  }

  // Do not return file content if only headers were requested.
  if (method == HTTP_HEAD) return;

  // Open requested file.
  File *file;
  st = File::Open(filename, "r", &file);
  if (!st.ok()) {
    if (st.code() == EACCES) {
      response->SendError(403, "Forbidden", nullptr);
    } else if (st.code() == ENOENT) {
      response->SendError(404, "Not Found", nullptr);
    } else {
      string error = HTMLEscape(st.message());
      response->SendError(500, "Internal Server Error", error.c_str());
    }
    return;
  }

  // Set content length to file size.
  response->set_content_length(stat.size);

  // Return file contents.
  response->SendFile(file);
}

}  // namespace sling


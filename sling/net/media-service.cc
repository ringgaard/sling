// Copyright 2020 Ringgaard Research ApS
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

#include "sling/net/media-service.h"

#include "sling/db/dbclient.h"
#include "sling/net/http-server.h"
#include "sling/net/http-utils.h"

namespace sling {

MediaService::MediaService(const string &url, const string &dbname)
    : url_(url) {
  if (!dbname.empty()) {
    CHECK(db_.Connect(dbname));
    LOG(INFO) << "Serve " << url << " from database " << dbname;
  }
}

void MediaService::Register(HTTPServer *http) {
  http->Register(url_, this, &MediaService::Handle);
}

void MediaService::Handle(HTTPRequest *request, HTTPResponse *response) {
  // Only GET and HEAD methods allowed.
  HTTPMethod method = GetHTTPMethod(request->method());
  if (method != HTTP_GET && method != HTTP_HEAD) {
    response->SendError(405, "Method Not Allowed", nullptr);
    return;
  }

  // Bail out if there is no media database.
  if (!db_.connected()) {
    response->SendError(404, "No Media Database");
    return;
  }

  // Get path.
  string path;
  if (!DecodeURLComponent(request->path(), &path)) {
    response->SendError(400, "Bad Request", nullptr);
    return;
  }
  if (path.empty() || path[0] != '/') {
    response->SendError(404, "Index Browsing Not Supported");
    return;
  }
  path.erase(0, 1);
  LOG(INFO) << "media url: " << path;

  // Retrieve media from database.
  MutexLock lock(&mu_);
  DBRecord media;
  Status st = db_.Get(path, &media);
  if (!st.ok()) {
    response->SendError(500, "Internal Server Error", st.message());
  }

  // Return error or redirect if file not found.
  size_t size = media.value.size();
  if (size == 0) {
    if (redirect_) {
      response->TempRedirectTo(path.c_str());
    } else {
      response->SendError(404, "File Not Found");
    }
    return;
  }

  // Check if file has changed.
  time_t mtime = media.version;
  if (mtime != 0) {
    const char *cached = request->Get("If-modified-since");
    const char *control = request->Get("Cache-Control");
    bool refresh = control != nullptr && strcmp(control, "maxage=0") == 0;
    if (!refresh && cached) {
      if (ParseRFCTime(cached) == mtime) {
        response->set_status(304);
        response->set_content_length(0);
        return;
      }
    }

    // Set file modified time.
    char datebuf[RFCTIME_SIZE];
    response->Set("Last-Modified", RFCTime(mtime, datebuf));
  }

  // Set content type from file extension.
  const char *mimetype = GetMimeType(GetExtension(path.c_str()));
  if (mimetype != nullptr) {
    response->set_content_type(mimetype);
  }

  // Do not return file content if only headers were requested.
  if (method == HTTP_HEAD) return;

  // Return media content.
  response->Append(media.value.data(), size);
  response->set_content_length(size);
}

}  // namespace sling


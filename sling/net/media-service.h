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

#ifndef SLING_NET_MEDIA_SERVICE_H_
#define SLING_NET_MEDIA_SERVICE_H_

#include <string>

#include "sling/base/types.h"
#include "sling/db/dbclient.h"
#include "sling/net/http-server.h"
#include "sling/util/mutex.h"

namespace sling {

// HTTP handler for serving media files from a database.
class MediaService {
 public:
  // Initialize handler for serving files from database.
  MediaService(const string &url, const string &dname);

  // Register handler with HTTP server.
  void Register(HTTPServer *http);

  // Serve static web content from database.
  void Handle(HTTPRequest *request, HTTPResponse *response);

  // Redirect to URL if media file is not found.
  bool redirect() const { return redirect_; }
  void set_redirect(bool b) { redirect_ = b; }

 private:
  // URL path for media content.
  string url_;

  // Media database.
  DBClient db_;

  // Mutex for serializing access to database.
  Mutex mu_;

  // Redirect request to URL key.
  bool redirect_ = false;
};

}  // namespace sling

#endif  // SLING_NET_MEDIA_SERVICE_H_


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

#ifndef SLING_NET_PROXY_SERVICE_H_
#define SLING_NET_PROXY_SERVICE_H_

#include <unordered_set>

#include "sling/net/http-server.h"

namespace sling {

// HTTP proxy service.
class ProxyService {
 public:
  // Initialize proxy service.
  ProxyService();
  ~ProxyService();

  // Register handler with HTTP server.
  void Register(HTTPServer *http);

  // Serve proxy requests.
  void Handle(HTTPRequest *request, HTTPResponse *response);

 private:
  // Callback from curl for receiving data.
  static size_t Header(char *buffer, size_t size, size_t n, void *userdata);

  // Callback from curl for receiving data.
  static size_t Data(void *buffer, size_t size, size_t n, void *userdata);

  // Header fields that are not passed through.
  static std::unordered_set<string> blocked_headers;
};

}  // namespace sling

#endif  // SLING_NET_PROXY_SERVICE_H_


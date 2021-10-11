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

#ifndef SLING_NLP_KB_SCHEMA_SERVICE_H_
#define SLING_NLP_KB_SCHEMA_SERVICE_H_

#include <string>

#include "sling/base/types.h"
#include "sling/frame/object.h"
#include "sling/frame/store.h"
#include "sling/net/http-server.h"

namespace sling {

// HTTP handler for serving schemas from knowledge base.
class SchemaService {
 public:
  // Initialize handler for serving schemas from knowledge base.
  SchemaService(Store *kb);

  // Register handler with HTTP server.
  void Register(HTTPServer *http);

  // Serve schemas.
  void HandleSchema(HTTPRequest *request, HTTPResponse *response);

 private:
  // Pre-encoded schemas.
  string encoded_schemas_;

  // Timestamp for cache control.
  time_t timestamp_;
};

}  // namespace sling

#endif  // SLING_NLP_KB_SCHEMA_SERVICE_H_


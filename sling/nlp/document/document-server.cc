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

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/http/http-server.h"
#include "sling/http/static-content.h"

DEFINE_int32(port, 8080, "HTTP server port");

using namespace sling;

class DocumentService {
 public:
  // Register service.
  void Register(HTTPServer *http) {
    app_content_.Register(http);
    common_content_.Register(http);
  }

 private:
  // Document app.
  StaticContent app_content_{"/doc", "sling/nlp/document/app"};

  // Common web components.
  StaticContent common_content_{"/common", "app"};
};

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  LOG(INFO) << "Start HTTP server on port " << FLAGS_port;
  HTTPServerOptions options;
  HTTPServer http(options, FLAGS_port);

  DocumentService service;
  service.Register(&http);

  //http.Register("/", [](HTTPRequest *req, HTTPResponse *rsp) {
  //  rsp->RedirectTo("/doc/");
  //});

  http.Register("/favicon.ico", [](HTTPRequest *req, HTTPResponse *rsp) {
    rsp->RedirectTo("/common/image/appicon.ico");
  });

  CHECK(http.Start());

  LOG(INFO) << "HTTP server running";
  http.Wait();

  LOG(INFO) << "HTTP server done";
  return 0;
}

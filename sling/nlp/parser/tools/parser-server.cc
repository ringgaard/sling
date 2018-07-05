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
#include "sling/http/web-service.h"
#include "sling/nlp/document/document-tokenizer.h"
#include "sling/nlp/parser/parser.h"

DEFINE_int32(port, 8080, "HTTP server port");
DEFINE_string(parser, "", "Parser model");

using namespace sling;
using namespace sling::nlp;

class ParserService {
 public:
  void Init(const string &parser_flow) {
    parser_.Load(&commons_, parser_flow);
    commons_.Freeze();
  }

  void Register(HTTPServer *http) {
    http->Register("/parse", this, &ParserService::HandleQuery);
  }

  void HandleQuery(HTTPRequest *request, HTTPResponse *response) {
    WebService ws(&commons_, request, response);

    // Get input document.
    Document *document;
    if (ws.input().IsFrame()) {
      document = new Document(ws.input().AsFrame(), &docnames_);
    } else {
      document = new Document(ws.store(), &docnames_);
      if (ws.input().IsString()) {
        document->SetText(ws.input().AsString().text());
      } else {
        document->SetText(ws.Get("text"));
      }
    }

    // Tokenize document.
    tokenizer_.Tokenize(document);

    // Parse document.
    parser_.Parse(document);

    // Return response.
    document->Update();
    ws.set_output(document->top());

    delete document;
  }

 private:
  Store commons_;
  DocumentNames docnames_{&commons_};
  DocumentTokenizer tokenizer_;
  Parser parser_;
};

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  LOG(INFO) << "Start HTTP server on port " << FLAGS_port;
  HTTPServerOptions options;
  HTTPServer http(options, FLAGS_port);

  ParserService service;
  service.Init(FLAGS_parser);
  service.Register(&http);

  CHECK(http.Start());
  LOG(INFO) << "HTTP server running";
  http.Wait();

  return 0;
}


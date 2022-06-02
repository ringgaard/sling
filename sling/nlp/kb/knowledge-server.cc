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
#include "sling/frame/serialization.h"
#include "sling/net/http-server.h"
#include "sling/net/media-service.h"
#include "sling/nlp/kb/knowledge-service.h"
#include "sling/nlp/kb/schema-service.h"

DEFINE_string(host, "", "HTTP server host address");
DEFINE_int32(port, 8080, "HTTP server port");
DEFINE_string(kb, "data/e/kb/kb.sling", "Knowledge base");
DEFINE_string(names, "data/e/kb/en/name-table.repo", "Name table");
DEFINE_string(xref, "", "Cross-reference table");
DEFINE_string(search, "", "Search index");
DEFINE_string(items, "", "Off-line items");
DEFINE_string(itemdb, "", "Database for off-line items");
DEFINE_string(mediadb, "", "Media database");

using namespace sling;
using namespace sling::nlp;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  LOG(INFO) << "Loading knowledge base from " << FLAGS_kb;
  Store commons;
  LoadStore(FLAGS_kb, &commons);

  LOG(INFO) << "Start HTTP server on port " << FLAGS_port;
  SocketServerOptions options;
  HTTPServer http(options, FLAGS_host.c_str(), FLAGS_port);

  KnowledgeService kb;
  kb.Load(&commons, FLAGS_names);
  if (!FLAGS_xref.empty()) {
    LOG(INFO) << "Loading xref from " << FLAGS_xref;
    kb.LoadXref(FLAGS_xref);
  }
  if (!FLAGS_search.empty()) {
    LOG(INFO) << "Loading search index from " << FLAGS_search;
    kb.LoadSearchIndex(FLAGS_search);
  }
  if (!FLAGS_items.empty()) {
    LOG(INFO) << "Open item set " << FLAGS_items;
    kb.OpenItems(FLAGS_items);
  }
  if (!FLAGS_itemdb.empty()) {
    LOG(INFO) << "Conect to item database " << FLAGS_itemdb;
    kb.OpenItemDatabase(FLAGS_itemdb);
  }

  commons.Freeze();

  SchemaService schemas(&commons);

  MediaService media("/media", FLAGS_mediadb);
  media.set_redirect(true);
  media.Register(&http);
  http.Register("/thumb", &media, &MediaService::Handle);

  kb.Register(&http);
  schemas.Register(&http);

  http.Register("/", [](HTTPRequest *req, HTTPResponse *rsp) {
    rsp->TempRedirectTo("/kb");
  });

  CHECK(http.Start());

  LOG(INFO) << "HTTP server running";
  http.Wait();

  LOG(INFO) << "HTTP server done";
  return 0;
}


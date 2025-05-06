// Copyright 2025 Ringgaard Research ApS
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

#include <signal.h>
#include <vector>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/file/recordio.h"
#include "sling/net/http-server.h"
#include "sling/nlp/search/search-engine.h"
#include "sling/util/json.h"
#include "sling/util/mutex.h"

using namespace sling;

DEFINE_string(addr, "", "HTTP server address");
DEFINE_int32(port, 7575, "HTTP server port");
DEFINE_int32(workers, 16, "Number of network worker threads");

RecordFileOptions itemdb_options;

// Each search engine shard indexes a subset of the documents/items. It has a
// free-text search engine and an optional item database.
struct SearchShard {
  ~SearchShard() {
    delete database;
  }

  // Load shard.
  void Load(const string &name, const string &repo, const string &items) {
    this->name = name;
    engine.Load(repo);
    if (!items.empty()) {
      database = new RecordDatabase(items, itemdb_options);
    }
  }

  // Search shard name.
  string name;

  // Search engine for search shard.
  nlp::SearchEngine engine;

  // Item database.
  RecordDatabase *database = nullptr;
};

// Search engine service.
class SearchService {
 public:
  ~SearchService() {
    for (SearchShard *shard : shards_) {
      LOG(INFO) << "Unload shard " << shard->name;
      delete shard;
    }
  }

  void Register(HTTPServer *http) {
    http->Register("/search", this, &SearchService::HandleSearch);
    http->Register("/load", this, &SearchService::HandleLoad);
    http->Register("/unload", this, &SearchService::HandleUnload);
  }

  void HandleSearch(HTTPRequest *request, HTTPResponse *response) {
    // Get parameters.
    URLQuery query(request->query());
    Text q = query.Get("q");
    Text tag = query.Get("tag");
    int limit = query.Get("limit", 50);

    // Find search shard.
    MutexLock lock(&mu_);
    SearchShard *shard = Find(tag);
    if (shard == nullptr) {
      response->SendError(400, nullptr, "Search shard not loaded");
      return;
    }

    // Search for hits in shard.
    nlp::SearchEngine::Results result(limit);
    int total = shard->engine.Search(q, &result);

    // Return result.
    JSON::Object json;
    json.Add("total", total);
    JSON::Array *hits = json.AddArray("hits");
    for (const nlp::SearchEngine::Hit &hit : result.hits()) {
      JSON::Object *result = hits->AddObject();
      result->Add("docid", hit.id());
      result->Add("score", hit.score);
    }

    json.Write(response->buffer());
    response->set_content_type("text/json");
    response->set_status(200);
  }

  void HandleLoad(HTTPRequest *request, HTTPResponse *response) {
    // Get parameters.
    URLQuery query(request->query());
    string name = query.Get("name").str();
    string repo = query.Get("repo").str();
    string items = query.Get("items").str();
    if (name.empty()) {
      response->SendError(400, nullptr, "Missing search shard name");
      return;
    }

    MutexLock lock(&mu_);
    if (Find(name) != nullptr) {
      response->SendError(400, nullptr, "Search shard already loaded");
      return;
    }
    SearchShard *shard = new SearchShard();
    shard->Load(name, repo, items);
    shards_.push_back(shard);
    LOG(INFO) << "Search shard " << name << " loaded";
  }

  void HandleUnload(HTTPRequest *request, HTTPResponse *response) {
    // Get parameters.
    URLQuery query(request->query());
    string name = query.Get("name").str();

    MutexLock lock(&mu_);
    SearchShard *shard = Find(name);
    if (shard == nullptr) {
      response->SendError(400, nullptr, "Search shard not loaded");
      return;
    }
    shards_.erase(find(shards_.begin(), shards_.end(), shard));
    delete shard;
    LOG(INFO) << "Search shard " << name << " unloaded";
  }

  SearchShard *Find(Text name) {
    for (SearchShard *shard : shards_) {
      if (shard->name == name) return shard;
    }
    return nullptr;
  }

 private:
  // Loaded search shards.
  std::vector<SearchShard *> shards_;

  // Mutex for accessing global server state.
  Mutex mu_;
};

// HTTP server.
HTTPServer *httpd = nullptr;

// Search service.
SearchService *search_service = nullptr;

// Termination handler.
void terminate(int signum) {
  VLOG(1) << "Shutdown requested";
  if (httpd != nullptr) httpd->Shutdown();
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Initialize database service.
  search_service = new SearchService();

  // Install signal handlers to handle termination.
  signal(SIGTERM, terminate);
  signal(SIGINT, terminate);

  // Start HTTP server.
  LOG(INFO) << "Start HTTP server on port " << FLAGS_port;
  SocketServerOptions sockopts;
  sockopts.num_workers = FLAGS_workers;
  httpd = new HTTPServer(sockopts, FLAGS_addr.c_str(), FLAGS_port);
  search_service->Register(httpd);
  CHECK(httpd->Start());
  LOG(INFO) << "Search engine running";
  httpd->Wait();

  // Shut down.
  LOG(INFO) << "Shutting down HTTP server";
  delete httpd;
  httpd = nullptr;

  LOG(INFO) << "Shutting down search engine";
  delete search_service;
  search_service = nullptr;

  LOG(INFO) << "Done";
  return 0;
}

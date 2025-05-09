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
#include "sling/nlp/search/search-protocol.h"
#include "sling/util/json.h"
#include "sling/util/mutex.h"

using namespace sling;

DEFINE_string(addr, "", "HTTP server address");
DEFINE_int32(port, 7575, "HTTP server port");
DEFINE_int32(workers, 16, "Number of network worker threads");

RecordFileOptions itemdb_options;

class SearchSession;

// Each search engine shard indexes a subset of the documents/items. It has a
// free-text search engine and an optional item database.
struct SearchShard {
  ~SearchShard() {
    delete database;
  }

  // Load shard.
  void Load(const string &name,
      const string &repo,
      const string &items,
      const string &prefix) {
    this->name = name;
    this->repofn = repo;
    this->itemsfn = items;

    engine.Load(repo);
    if (!items.empty()) {
      database = new RecordDatabase(items, itemdb_options);
    }
    idprefix = prefix;
  }

  // Check shard has item id.
  bool Has(Text id) const {
    if (!database) return false;
    if (idprefix.empty()) return false;
    return id.starts_with(idprefix);
  }

  bool fetchable() const { return !idprefix.empty(); }

  // Search shard name.
  string name;

  // Search repo file.
  string repofn;

  // Item database.
  string itemsfn;

  // Prefix for itemids.
  string idprefix;

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
    http->Register("/statusz", this, &SearchService::HandleStatusz);
    http->Register("/", this, &SearchService::HandleUpgrade);
  }

  void HandleStatusz(HTTPRequest *request, HTTPResponse *response) {
    // General server information.
    JSON::Object json;
    json.Add("time", time(nullptr));

    // Output loaded shards.
    JSON::Array *shards = json.AddArray("shards");
    MutexLock lock(&mu_);
    for (auto *shard : shards_) {
      JSON::Object *s = shards->AddObject();
      s->Add("name", shard->name);
      s->Add("repo", shard->repofn);
      if (!shard->itemsfn.empty()) s->Add("items", shard->itemsfn);
      if (!shard->idprefix.empty()) s->Add("idprefix", shard->idprefix);
    }

    json.Write(response->buffer());
    response->set_content_type("application/json");
  }

  void HandleUpgrade(HTTPRequest *request, HTTPResponse *response) {
    if (request->Method() == HTTP_GET && strcmp(request->path(), "/") == 0) {
      // Check for upgrade request.
      const char *connection = request->Get("Connection");
      const char *upgrade = request->Get("Upgrade");
      if (connection == nullptr || strcasecmp(connection, "upgrade") != 0 ||
          upgrade == nullptr || strcasecmp(upgrade, "search") != 0) {
        response->SendError(404);
        return;
      }

      // Upgrade to search protocol.
      const char *ua = request->Get("User-Agent");
      SocketSession *client = NewClient(this, request->conn(), ua);
      response->Upgrade(client);
      response->set_status(101);
      response->Set("Connection", "upgrade");
      response->Set("Upgrade", "search");
    } else {
      response->SendError(404);
    }
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
    string idprefix = query.Get("idprefix").str();
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
    shard->Load(name, repo, items, idprefix);
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

  Status Search(const JSON &query, JSON::Object *response) {
    // Get search parameters.
    Text q = query["q"];
    Text tag = query["tag"];
    int limit = query["limit"].i(50);

    // Find search shard.
    MutexLock lock(&mu_);
    SearchShard *shard = Find(tag);
    if (shard == nullptr) return Status(ENOENT, "shard not found");

    // Search for hits in shard.
    nlp::SearchEngine::Results result(limit);
    int total = shard->engine.Search(q, &result);

    // Return result.
    response->Add("total", total);
    response->Add("fetchable", shard->fetchable());
    JSON::Array *hits = response->AddArray("hits");
    for (const nlp::SearchEngine::Hit &hit : result.hits()) {
      JSON::Object *result = hits->AddObject();
      result->Add("docid", hit.id());
      result->Add("score", hit.score);
    }

    return Status::OK;
  }

  bool Fetch(IOBuffer *request, IOBuffer *response) {
    MutexLock lock(&mu_);
    Record record;
    SearchShard *shard = nullptr;
    while (request->available() > 0) {
      // Read next document id.
      uint8 klen;
      if (!request->Read(&klen, 1)) return false;
      if (request->available() < klen) return false;
      Text key(request->Consume(klen), klen);

      // Find shard for item id.
      if (!shard || !shard->Has(key)) shard = FindForId(key);

      // Try to fetch record for item.
      if (shard) {
        if (!shard->database->Lookup(key, &record)) continue;

        // Write record to response.
        uint32 size = record.value.size();
        response->Write(&size, 4);
        response->Write(record.value.data(), record.value.size());
      }
    }

    return true;
  }

  SearchShard *Find(Text name) {
    for (SearchShard *shard : shards_) {
      if (shard->name == name) return shard;
    }
    return nullptr;
  }

  SearchShard *FindForId(Text docid) {
    for (SearchShard *shard : shards_) {
      if (shard->Has(docid)) return shard;
    }
    return nullptr;
  }

  static SocketSession *NewClient(SearchService *search,
                                  SocketConnection *conn,
                                  const char *ua);

 private:
  // Loaded search shards.
  std::vector<SearchShard *> shards_;

  // Mutex for accessing global server state.
  Mutex mu_;
};

// Search session that uses the SLING search protocol.
class SearchSession : public SocketSession {
 public:
  SearchSession(SearchService *search, SocketConnection *conn, const char *ua) {
    conn_ = conn;
    search_ = search;
    if (ua) agent_ = strdup(ua);
  }

  ~SearchSession() override {
    free(agent_);
  }

  const char *Name() override { return "search"; }
  const char *Agent() override { return agent_ ? agent_ : ""; }
  int IdleTimeout() override  { return 86400; }

  Continuation Process(SocketConnection *conn) override {
    // Check if we have received a complete header.
    auto *req = conn->request();
    if (req->available() < sizeof(SPHeader)) return CONTINUE;

    // Check if request body has been received.
    auto *hdr = SPHeader::from(req->begin());
    if (req->available() < hdr->size + sizeof(SPHeader)) return CONTINUE;

    // Dispatch request.
    req->Consume(sizeof(SPHeader));
    if (req->available() != hdr->size) return TERMINATE;
    Continuation cont = TERMINATE;
    switch (hdr->verb) {
      case SPSEARCH: cont = Search(); break;
      case SPFETCH: cont = Fetch(); break;
      default: return Error("command verb not supported");
    }

    // Make sure the whole request has been consumed.
    if (req->available() > 0) req->Consume(req->available());

    return cont;
  }

  Continuation Search() {
    // Parse search result as JSON.
    JSON query = JSON::Read(conn_->request());
    if (!query.valid()) return TERMINATE;

    JSON::Object response;
    Status st = search_->Search(query, &response);
    if (!st.ok()) return Error(st.message());

    response.Write(conn_->response_body());
    return Response(SPRESULT);
  }

  Continuation Fetch() {
    if (!search_->Fetch(conn_->request(), conn_->response_body())) {
      return TERMINATE;
    }
    return Response(SPITEMS);
  }

  Continuation Error(const char *msg) {
    // Clear existing (partial) response.
    conn_->response_header()->Clear();
    conn_->response_body()->Clear();

    // Return error message.
    int msgsize = strlen(msg);
    conn_->response_body()->Write(msg, msgsize);

    return Response(SPERROR);
  }

  Continuation Response(SPVerb verb) {
    SPHeader *hdr = conn_->response_header()->append<SPHeader>();
    hdr->verb = verb;
    hdr->size = conn_->response_body()->available();
    return RESPOND;
  }

 private:
  SearchService *search_;         // search service
  SocketConnection *conn_;        // client connection
  char *agent_ = nullptr;         // user agent
};

SocketSession *SearchService::NewClient(
    SearchService *search,
    SocketConnection *conn,
    const char *ua) {
  return new SearchSession(search, conn, ua);
}

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

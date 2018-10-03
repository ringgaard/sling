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
#include "sling/file/recordio.h"
#include "sling/frame/serialization.h"
#include "sling/http/http-server.h"
#include "sling/http/static-content.h"
#include "sling/http/web-service.h"
#include "sling/nlp/document/document.h"
#include "sling/util/mutex.h"

DEFINE_int32(port, 8080, "HTTP server port");
DEFINE_string(commons, "", "Commons store");

using namespace sling;

class DocumentService {
 public:
  DocumentService(Store *commons, RecordDatabase *db)
      : commons_(commons), db_(db) {
    CHECK(names_.Bind(commons));
  }

  // Register service.
  void Register(HTTPServer *http) {
    http->Register("/fetch", this, &DocumentService::HandleFetch);
    app_content_.Register(http);
    common_content_.Register(http);
  }

  void HandleFetch(HTTPRequest *request, HTTPResponse *response) {
    WebService ws(commons_, request, response);
    Text docid = ws.Get("docid");
    if (docid.empty()) {
      response->SendError(400, nullptr, "docid missing");
      return;
    }
    LOG(INFO) << "docid: " << docid;

    // Fetch document from database.
    Record record;
    if (!FetchRecord(docid, &record)) {
      response->SendError(400, nullptr, "unknown document");
      return;
    }

    Store *store = ws.store();
    Frame top = Decode(store, record.value).AsFrame();
    nlp::Document document(top);

    // Builds client-side frame list.
    FrameMapping mapping(store);
    Handles mentions(store);
    Handles themes(store);
    mapping.Add(Handle::isa());
    mapping.Add(Handle::is());
    mapping.Add(n_name_.handle());

    // Add all evoked frames.
    Handles queue(store);
    for (int i = 0; i < document.num_spans(); ++i) {
      nlp::Span *span = document.span(i);
      if (span->deleted()) continue;
      const Frame &mention = span->mention();

      // Add the mention frame.
      if (mapping.Add(mention.handle())) {
        queue.push_back(mention.handle());
        mentions.push_back(mention.handle());
      }

      // Add all evoked frames.
      for (const Slot &slot : mention) {
        if (slot.name != n_evokes_) continue;

        // Queue all evoked frames.
        Handle h = slot.value;
        if (!store->IsFrame(h)) continue;
        if (mapping.Add(h)) {
          queue.push_back(h);
        }
      }
    }

    // Add thematic frames.
    for (Handle h : document.themes()) {
      if (!store->IsFrame(h)) continue;
      if (mapping.Add(h)) {
        queue.push_back(h);
      }
      themes.push_back(h);
    }

    // Process queue.
    int current = 0;
    while (current < queue.size()) {
      // Process all slot names and values for next frame in queue.
      Frame frame(store, queue[current++]);
      for (const Slot &slot : frame) {
        if (store->IsFrame(slot.name)) {
          if (mapping.Add(slot.name)) {
            // Only add local frames to queue.
            if (slot.name.IsLocalRef()) queue.push_back(slot.name);
          }
        }
        if (store->IsFrame(slot.value)) {
          if (mapping.Add(slot.value)) {
            // Only add local frames to queue.
            if (slot.value.IsLocalRef()) queue.push_back(slot.value);
          }
        }
      }
    }

    // Set document text and tokens.
    Builder b(ws.store());
    b.Add(n_text_, document.text());
    b.Add(n_tokens_, top.GetHandle(n_tokens_));

    // Output frame list.
    Handles frames(store);
    Handles types(store);
    Handles roles(store);
    String idstr(store, "id");
    for (Handle handle : mapping.frames) {
      // Collect id, name, description, types, and other roles for frame.
      bool simple = false;
      Handle id = Handle::nil();
      Handle name = Handle::nil();
      Handle description = Handle::nil();
      types.clear();
      roles.clear();
      if (store->IsFrame(handle)) {
        Frame frame(store, handle);
        for (const Slot &slot : frame) {
          if (slot.name == Handle::id() && store->IsSymbol(slot.value)) {
            if (id.IsNil()) id = slot.value;
          } else if (slot.name == n_name_ && store->IsString(slot.value)) {
            if (name.IsNil()) name = slot.value;
          } else if (slot.name == n_description_ &&
                     store->IsString(slot.value)) {
            if (description.IsNil())  description = slot.value;
          } else if (slot.name.IsIsA()) {
            int idx = mapping.Lookup(slot.value);
            if (idx != -1) {
              types.push_back(Handle::Integer(idx));
            } else {
              Frame type(store, slot.value);
              if (type.valid()) {
                Handle type_id = type.id().handle();
                if (!type_id.IsNil()) types.push_back(type_id);
                if (type.GetBool(n_simple_)) simple = true;
              }
            }
          } else {
            roles.push_back(mapping.Convert(slot.name));
            roles.push_back(mapping.Convert(slot.value));
          }
        }
      } else if (store->IsSymbol(handle)) {
        id = handle;
      }

      // Add frame to list.
      Builder fb(store);
      fb.Add(idstr, id);
      fb.Add(n_name_, name);
      fb.Add(n_description_, description);
      if (simple) fb.Add(n_simple_, true);
      frames.push_back(fb.Create().handle());
    }
    b.Add(n_frames_, frames);

    // Return response.
    ws.set_output(b.Create());
  }

  bool FetchRecord(Text key, Record *record) {
    MutexLock lock(&mu_);
    return db_->Lookup(key.slice(), record);
  }

 private:
  // Mapping between frames and indices.
  struct FrameMapping {
    FrameMapping(Store *store) : store(store), frames(store) {
      n_name = store->Lookup("name");
    }

    // Add frame to mapping.
    bool Add(Handle handle) {
      if (indices.find(handle) != indices.end()) return false;
      indices[handle] = frames.size();
      frames.push_back(handle);
      return true;
    }

    // Look up frame index for frame.
    int Lookup(Handle handle) {
      auto f = indices.find(handle);
      return f != indices.end() ? f->second : -1;
    }

    // Convert value to mapped representation where frames are integer indices
    // and other values are strings.
    Handle Convert(Handle value) {
      // Output null for nil values.
      if (value.IsNil()) return Handle::nil();

      if (store->IsFrame(value)) {
        // Output integer index for references to known frames.
        int frame_index = Lookup(value);
        if (frame_index != -1) return Handle::Integer(frame_index);

        // Output name or id as string for external frames.
        Handle literal = Handle::nil();
        Frame frame(store, value);
        if (frame.Has(n_name)) {
          literal = frame.GetHandle(n_name);
        } else {
          literal = frame.id().handle();
        }
        if (!literal.IsNil()) return literal;
      }

      // Output strings literally.
      if (store->IsString(value)) return value;

      // Otherwise output SLING text encoding.
      return store->AllocateString(ToText(store, value));
    }

    Store *store;            // frame store
    Handle n_name;           // name symbol
    Handles frames;          // frames by index
    HandleMap<int> indices;  // mapping from frame to index
  };

  // Commons store.
  Store *commons_;

  // Record database with documents.
  RecordDatabase *db_;

  // Static web content.
  StaticContent app_content_{"/doc", "sling/nlp/document/app"};
  StaticContent common_content_{"/common", "app"};

  // Symbol names.
  Names names_;
  Name n_name_{names_, "name"};
  Name n_description_{names_, "description"};
  Name n_text_{names_, "text"};
  Name n_tokens_{names_, "tokens"};
  Name n_frames_{names_, "frames"};
  Name n_mentions_{names_, "mentions"};
  Name n_themes_{names_, "themes"};
  Name n_evokes_{names_, "evokes"};
  Name n_simple_{names_, "simple"};

  // Mutex for accessing database.
  Mutex mu_;
};

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);
  std::vector<string> files;
  for (int i = 1; i < argc; ++i) {
    File::Match(argv[i], &files);
  }
  CHECK(!files.empty()) << "No document database files";

  // Open record database.
  RecordFileOptions recopts;
  RecordDatabase db(files, recopts);

  // Load commons store.
  Store commons;
  if (!FLAGS_commons.empty()) {
    LoadStore(FLAGS_commons, &commons);
  }

  // Initialize document service.
  DocumentService service(&commons, &db);
  commons.Freeze();

  LOG(INFO) << "Start HTTP server on port " << FLAGS_port;
  HTTPServerOptions httpopts;
  HTTPServer http(httpopts, FLAGS_port);

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

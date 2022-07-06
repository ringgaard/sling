// Copyright 2022 Ringgaard Research ApS
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
#include "sling/frame/object.h"
#include "sling/frame/reader.h"
#include "sling/frame/json.h"
#include "sling/nlp/kb/refine-service.h"
#include "sling/stream/memory.h"

DEFINE_string(kburl_prefix, "https://ringgaard,com", "KB service URL prefix");

namespace sling {
namespace nlp {

static const char *default_types[] = {
  "Q35120",     // entity
  "Q5",         // human
  "Q17334923",  // location
  "Q43229",     // organization
  "Q783794",    // company
  nullptr,
};

RefineService::RefineService(Store *commons, KnowledgeService *kb)
    : commons_(commons), kb_(kb), default_types_(commons_) {
  // Bind names.
  names_.Bind(commons);

  // Set up default types.
  for (const char **type = default_types; *type != nullptr; ++type) {
    Handle t = commons->LookupExisting(*type);
    if (!t.IsNil()) default_types_.push_back(t);
  }
}

void RefineService::Register(HTTPServer *http) {
  http->Register("/refine", this, &RefineService::HandleRefine);
  http->Register("/preview", this, &RefineService::HandlePreview);
}

void RefineService::HandleRefine(HTTPRequest *req, HTTPResponse *rsp) {
  switch (req->Method()) {
    case HTTP_GET: {
      URLQuery query(req->query());
      Text queries = query.Get("queries");
      if (!queries.empty()) {
        HandleQuery(queries, rsp);
      } else {
        HandleManifest(req, rsp);
      }
      break;
    }

    case HTTP_POST: {
      Text ct = req->content_type();
      int semi = ct.find(';');
      if (semi != -1) ct = ct.substr(0, semi);
      if (ct == "application/x-www-form-urlencoded") {
        Text body(req->content(), req->content_size());
        URLQuery query(body);
        Text queries = query.Get("queries");
        HandleQuery(queries, rsp);
      } else {
        URLQuery query(req->query());
        Text queries = query.Get("queries");
        HandleQuery(queries, rsp);
      }
      break;
    }

    default:
      rsp->SendError(405);
  }
}

void RefineService::HandleManifest(HTTPRequest *req, HTTPResponse *rsp) {
  Store store(commons_);
  String n_id(&store, "id");

  // Build OpenRefine manifest.
  Builder manifest(&store);
  manifest.Add(n_name_, "KnolBase");
  manifest.Add(n_id_space_, "http://www.wikidata.org/entity/");
  manifest.Add(n_schema_space_, "http://www.wikidata.org/prop/direct/");

  // Add view.
  Builder view(&store);
  view.Add(n_url_, FLAGS_kburl_prefix + "/kb/{{id}}");
  manifest.Add(n_view_, view.Create());

  // Add preview.
  Builder preview(&store);
  preview.Add(n_url_, FLAGS_kburl_prefix + "/preview/{{id}}");
  preview.Add(n_width_, 400);
  preview.Add(n_height_, 100);
  manifest.Add(n_preview_, preview.Create());

  // Add default types.
  Handles types(&store);
  for (Handle type : default_types_) {
    Frame property(&store, type);
    Builder b(&store);
    b.Add(n_id, property.Id());
    b.Add(n_name_, property.GetHandle(n_name_));
    types.push_back(b.Create().handle());
  }
  manifest.Add(n_default_types_, Array(&store, types));

  // Output as JSON.
  WriteJSON(manifest.Create(), rsp);

  // Add CORS headers.
  rsp->Add("Access-Control-Allow-Methods", "GET, POST");
  rsp->Add("Access-Control-Allow-Headers", "Origin, Accept, Content-Type");
  rsp->Add("Access-Control-Allow-Origin", "*");
}

void RefineService::HandleQuery(Text queries, HTTPResponse *rsp) {
  // Parse queries.
  Store store(commons_);
  String n_id(&store, "id");
  Frame input = ReadJSON(&store, queries.slice()).AsFrame();
  if (input.invalid()) {
    rsp->SendError(400);
    return;
  }

  // Process queries.
  Builder response(&store);
  for (const Slot &q : input) {
    // Get query.
    Builder result(&store);
    Frame request(&store, q.value);
    Text query = request.GetText(n_query_);
    int limit = request.GetInt(n_limit_, 50);

    // Search name table.
    NameTable::Matches matches;
    kb_->aliases().Lookup(query, false, 5000, 1, &matches);

    // Generate matches.
    Handles results(&store);
    for (const auto &m : matches) {
      if (results.size() >= limit) break;
      int score = m.first;
      Text id = m.second->id();
      Frame item(&store, kb_->RetrieveItem(&store, id));
      if (item.invalid()) continue;

      Builder match(&store);
      match.Add(n_id, item);
      match.Add(n_score_, score);

      Handle name = item.GetHandle(n_name_);
      if (!name.IsNil()) match.Add(n_name_, name);

      Handle description = item.GetHandle(n_description_);
      if (!description.IsNil()) match.Add(n_description_, description);

      results.push_back(match.Create().handle());
    }
    result.Add(n_result_, Array(&store, results));
    response.Add(q.name, result.Create());
  }

  // Output response.
  WriteJSON(response.Create(), rsp);
}

void RefineService::HandlePreview(HTTPRequest *req, HTTPResponse *rsp) {
  // Get item id.
  Text id = req->path();
  id.remove_prefix(1);

  // Fetch item.
  Store store(commons_);
  Frame item(&store, kb_->RetrieveItem(&store, id));
  if (item.invalid()) {
    rsp->SendError(500);
    return;
  }
  Text name = item.GetText(n_name_);
  Text description = item.GetText(n_description_);
  string image = kb_->GetImage(item);

  // Output preview html.
  rsp->Append(
    "<html>"
    "<head><meta charset=\"utf-8\" /></head>"
    "<body style=\"margin: 0px; font-family: Arial; sans-serif\">"
    "<div style=\"height: 100px; width: 400px; overflow: hidden; "
    "font-size: 0.7em\">"
  );

  if (!image.empty()) {
    rsp->Append(
      "<div style=\"width: 100px; text-align: center; "
      "overflow: hidden; margin-right: 9px; float: left\">"
      "<img src=\"");
    rsp->Append(image);
    rsp->Append("\" style=\"height: 100px\" /></div>");
  }

  rsp->Append("<div style=\"margin-left: 3px;\">");
  rsp->Append("<a href=\"");
  rsp->Append(FLAGS_kburl_prefix);
  rsp->Append("/");
  rsp->Append(HTMLEscape(id));
  rsp->Append("\" target=\"_blank\" style=\"text-decoration: none;\">");
  rsp->Append(HTMLEscape(name));
  rsp->Append("<span style=\"color: #505050;\"> (");
  rsp->Append(HTMLEscape(id));
  rsp->Append(")</span>");
  if (!description.empty()) {
    rsp->Append("<p>");
    rsp->Append(HTMLEscape(description));
    rsp->Append("</p>");
  }
  rsp->Append("</div>");


  rsp->Append("</div></body></html>");
  rsp->set_content_type("text/html");
}

Object RefineService::ReadJSON(Store *store, const Slice &data) {
  ArrayInputStream stream(data);
  Input in(&stream);
  Reader reader(store, &in);
  reader.set_json(true);
  return reader.Read();
}

void RefineService::WriteJSON(const Object &object, HTTPResponse *rsp) {
  rsp->set_content_type("application/json; charset=utf-8");
  IOBufferOutputStream stream(rsp->buffer());
  Output out(&stream);
  JSONWriter writer(object.store(), &out);
  writer.set_byref(false);
  writer.Write(object);
}

}  // namespace nlp
}  // namespace sling


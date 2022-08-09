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

DEFINE_string(kburl_prefix,
              "https://ringgaard.com",
              "KB service URL prefix");

DEFINE_string(openrefine_service_name,
              "KnolBase",
              "OpenRefine service name");

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

  // Initialize facts.
  facts_.Init(commons);
}

void RefineService::Register(HTTPServer *http) {
  http->Register("/reconcile", this, &RefineService::HandleReconcile);
  http->Register("/preview", this, &RefineService::HandlePreview);
  http->Register("/suggest", this, &RefineService::HandleSuggest);
}

void RefineService::HandleReconcile(HTTPRequest *req, HTTPResponse *rsp) {
  // Get parameters.
  Text qs = req->query();
  if (req->Method() == HTTP_POST) {
    qs = Text(req->content(), req->content_size());
  } else {
    qs = Text(req->query());
  }
  URLQuery query(qs);

  // Dispatch call.
  Text queries = query.Get("queries");
  Text extend = query.Get("extend");
  if (!queries.empty()) {
    HandleQuery(queries, rsp);
  } else if (!extend.empty()) {
    HandleExtend(extend, rsp);
  } else {
    HandleManifest(req, rsp);
  }
}

void RefineService::HandleManifest(HTTPRequest *req, HTTPResponse *rsp) {
  Store store(commons_);
  String n_id(&store, "id");

  // Build OpenRefine manifest.
  Builder manifest(&store);
  manifest.Add(n_name_, FLAGS_openrefine_service_name);
  manifest.Add(n_id_space_, "http://www.wikidata.org/entity/");
  manifest.Add(n_schema_space_, "http://www.wikidata.org/prop/direct/");

  // View.
  Builder view(&store);
  view.Add(n_url_, FLAGS_kburl_prefix + "/kb/{{id}}");
  manifest.Add(n_view_, view.Create());

  // Preview.
  Builder preview(&store);
  preview.Add(n_url_, FLAGS_kburl_prefix + "/preview/{{id}}");
  preview.Add(n_width_, 400);
  preview.Add(n_height_, 100);
  manifest.Add(n_preview_, preview.Create());

  // Suggest service.
  Builder suggest_entity(&store);
  suggest_entity.Add(n_service_url_, FLAGS_kburl_prefix);
  suggest_entity.Add(n_service_path_, "/suggest/entity");
  Builder suggest_property(&store);
  suggest_property.Add(n_service_url_, FLAGS_kburl_prefix);
  suggest_property.Add(n_service_path_, "/suggest/property");
  Builder suggest_type(&store);
  suggest_type.Add(n_service_url_, FLAGS_kburl_prefix);
  suggest_type.Add(n_service_path_, "/suggest/type");
  Builder suggest(&store);
  suggest.Add("entity", suggest_entity.Create());
  suggest.Add("property", suggest_property.Create());
  suggest.Add("type", suggest_type.Create());
  manifest.Add(n_suggest_, suggest.Create());

  // Extend service.
  Builder extend(&store);
  manifest.Add(n_extend_, extend.Create());

  // Default types.
  Handles types(&store);
  for (Handle type : default_types_) {
    Frame property(&store, type);
    Builder b(&store);
    b.Add(n_id, property.Id());
    b.Add(n_name_, property.GetHandle(n_name_));
    types.push_back(b.Create().handle());
  }
  manifest.Add(n_default_types_, types);

  // Versions supported.
  Array versions(&store, 2);
  versions.set(0, store.AllocateString("0.1"));
  versions.set(1, store.AllocateString("0.2"));
  manifest.Add(n_versions_, versions);

  // Output as JSON.
  WriteJSON(manifest.Create(), rsp);

  // Add CORS headers.
  rsp->Add("Access-Control-Allow-Methods", "GET, POST");
  rsp->Add("Access-Control-Allow-Headers", "Origin, Accept, Content-Type");
  rsp->Add("Access-Control-Allow-Origin", "*");
}

void RefineService::HandleQuery(Text queries, HTTPResponse *rsp) {
  // Parse queries.
  LOG(INFO) << "query: " << queries;
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
    int limit = request.GetInt(n_limit_, 10);
    Text type = request.GetText(n_type_);
    Handle itemtype = Handle::nil();
    if (!type.empty()) itemtype = commons_->LookupExisting(type);

    // Search name table.
    NameTable::Matches matches;
    kb_->aliases().Lookup(query, false, 5000, 1, &matches);

    // Sum up total score.
    int total = 0;
    for (const auto &m : matches) {
      total += m.first;
    }
    int threshold = total * 0.8;

    // Generate matches.
    Handles results(&store);
    for (const auto &m : matches) {
      if (results.size() >= limit) break;
      int score = m.first;
      Text id = m.second->id();
      Frame item(&store, kb_->RetrieveItem(&store, id));
      if (item.invalid()) continue;

      // Check item type.
      if (!itemtype.IsNil()) {
        if (!facts_.InstanceOf(item.handle(), itemtype)) continue;
      }

      // Add match.
      Builder match(&store);
      match.Add(n_id, item);
      match.Add(n_score_, score);
      if (score >= threshold) match.Add(n_match_, true);


      Handle name = item.GetHandle(n_name_);
      if (!name.IsNil()) match.Add(n_name_, name);

      Handle description = item.GetHandle(n_description_);
      if (!description.IsNil()) match.Add(n_description_, description);

      results.push_back(match.Create().handle());
    }
    result.Add(n_result_, results);
    response.Add(q.name, result.Create());
  }

  // Add CORS headers.
  rsp->Add("Access-Control-Allow-Origin", "*");

  // Output response.
  WriteJSON(response.Create(), rsp);
}

void RefineService::HandleExtend(Text extend, HTTPResponse *rsp) {
  // Parse extension request.
  LOG(INFO) << "extend: " << extend;
  Store store(commons_);
  String n_id(&store, "id");

  Frame input = ReadJSON(&store, extend.slice()).AsFrame();
  if (input.invalid()) {
    rsp->SendError(400);
    return;
  }

  Array ids = input.Get(n_ids_).AsArray();
  Array properties = input.Get(n_properties_).AsArray();
  if (ids.invalid() || properties.invalid()) {
    rsp->SendError(400);
    return;
  }

  // Add meta data to response.
  Builder response(&store);
  Handles props(&store);
  Handles meta(&store);
  for (int i = 0; i < properties.length(); ++i) {
    Frame property(&store, properties.get(i));
    if (property.invalid()) continue;
    Text pid = property.GetText("_id");
    Handle prop = store.LookupExisting(pid);
    if (prop.IsNil()) continue;

    props.push_back(prop);
    Frame p(&store, prop);

    Builder b(&store);
    b.Add(n_id, p.Id());
    b.Add(n_name_, p.GetText(n_name_));
    meta.push_back(b.Create().handle());
  }
  response.Add(n_meta_, meta);

  // Look up property values for items.
  Builder rows(&store);
  for (int i = 0; i < ids.length(); ++i) {
    Handle id = ids.get(i);
    if (!store.IsString(id)) continue;
    Text itemid = store.GetString(id)->str();

    Frame item(&store, kb_->RetrieveItem(&store, itemid));
    if (item.invalid()) continue;

    // TODO: handle date properties (date: "YYYY-MM-DD")
    Builder row(&store);
    for (Handle prop : props) {
      Handles values(&store);
      for (const Slot &s : item.Slots(prop)) {
        Handle value = store.Resolve(s.value);
        Builder v(&store);
        if (value.IsInt()) {
          v.Add(n_int_, value);
        } else if (value.IsFloat()) {
          v.Add(n_float_, value);
        } else {
          const Datum *datum = store.GetObject(value);
          if (datum->IsString()) {
            v.Add(n_str_, value);
          } else if (datum->IsFrame() && datum->AsFrame()->IsPublic()) {
            Frame frame(&store, value);
            v.Add(n_id, frame.Id());
            if (frame.Has(n_name_)) {
              v.Add(n_name_, frame.Get(n_name_));
            }
            if (frame.Has(n_description_)) {
              v.Add(n_description_, frame.Get(n_description_));
            }
          }
        }
        values.push_back(v.Create().handle());
      }
      row.Add(prop, values);
    }
    rows.Add(id, row.Create());
  }
  response.Add(n_rows_, rows.Create());

  // Add CORS headers.
  rsp->Add("Access-Control-Allow-Origin", "*");

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
    "<body style=\"margin: 0px; font: 0.7em sans-serif; overflow: hidden\">"
    "<div style=\"height: 100px; width: 400px; display: flex\">"
  );

  if (!image.empty()) {
    rsp->Append("<img src=\"");
    rsp->Append(FLAGS_kburl_prefix);
    rsp->Append("/media/");
    rsp->Append(image);
    rsp->Append("\" style=\"float: left\" />");
  }

  rsp->Append("<div style=\"padding-left: 5px\"><div>");
  rsp->Append("<a href=\"");
  rsp->Append(FLAGS_kburl_prefix);
  rsp->Append("/kb/");
  rsp->Append(HTMLEscape(id));
  rsp->Append("\" target=\"_blank\" style=\"text-decoration: none;\">");
  rsp->Append(HTMLEscape(name));
  rsp->Append("</a>");
  rsp->Append("<span style=\"color: #505050;\"> (");
  rsp->Append(HTMLEscape(id));
  rsp->Append(")</span></div>");
  if (!description.empty()) {
    rsp->Append("<p>");
    rsp->Append(HTMLEscape(description));
    rsp->Append("</p>");
  }
  rsp->Append("</div>");

  rsp->Append("</div></body></html>");
  rsp->set_content_type("text/html");
}

void RefineService::HandleSuggest(HTTPRequest *req, HTTPResponse *rsp) {
  // Get query parameters.
  Text path = req->path();
  URLQuery params(req->query());
  Text prefix = params.Get("prefix");
  bool prefixed = params.Get("prefixed", true);
  bool properties = path.starts_with("/property");
  bool types = path.starts_with("/type");

  // Search name table.
  NameTable::Matches matches;
  kb_->aliases().Lookup(prefix, prefixed, 5000, 1000, &matches);

  // Generate matches.
  Store store(commons_);
  String n_id(&store, "id");
  Handles results(&store);
  const static int limit = 50;
  for (const auto &m : matches) {
    if (results.size() >= limit) break;

    Text id = m.second->id();
    Frame item(&store, kb_->RetrieveItem(&store, id));
    if (item.invalid()) continue;

    if (item.IsA(n_property_) != properties) continue;
    if (types && !IsType(item)) continue;

    Builder match(&store);
    match.Add(n_id, item);

    Handle name = item.GetHandle(n_name_);
    if (!name.IsNil()) match.Add(n_name_, name);

    Handle description = item.GetHandle(n_description_);
    if (!description.IsNil()) match.Add(n_description_, description);

    results.push_back(match.Create().handle());
  }

  Builder response(&store);
  response.Add(n_result_, results);

  // Add CORS headers.
  rsp->Add("Access-Control-Allow-Origin", "*");

  // Output response.
  WriteJSON(response.Create(), rsp);
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


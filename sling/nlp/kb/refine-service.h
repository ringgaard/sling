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

// W3C Reconciliation Service API

#ifndef NLP_KB_REFINE_SERVICE_H_
#define NLP_KB_REFINE_SERVICE_H_

#include "sling/frame/store.h"
#include "sling/net/http-server.h"
#include "sling/nlp/kb/facts.h"
#include "sling/nlp/kb/knowledge-service.h"

namespace sling {
namespace nlp {

// OpenRefine reconciliation service.
class RefineService {
 public:
  RefineService(Store *commons, KnowledgeService *kb);

  // Register OpenRefine API endpoints.
  void Register(HTTPServer *http);

  // OpenRefine request handlers.
  void HandleReconcile(HTTPRequest *req, HTTPResponse *rsp);
  void HandleManifest(HTTPRequest *req, HTTPResponse *rsp);
  void HandleQuery(Text queries, HTTPResponse *rsp);
  void HandleExtend(Text extend, HTTPResponse *rsp);
  void HandlePreview(HTTPRequest *req, HTTPResponse *rsp);
  void HandleSuggest(HTTPRequest *req, HTTPResponse *rsp);
  void HandlePropose(HTTPRequest *req, HTTPResponse *rsp);

 private:
  // Get representative image for item.
  string GetImage(const Frame &item);

  // Check if item is a type.
  bool IsType(const Frame &item) const {
    if (item.Has(n_subclass_of_)) return true;
    if (item == n_entity_) return true;
    return false;
  }

  // Read JSON request.
  static Object ReadJSON(Store *store, const Slice &slice);
  static Object ReadJSON(Store *store, HTTPRequest *req) {
    return ReadJSON(store, Slice(req->content(), req->content_size()));
  }

  // Write JSON response.
  static void WriteJSON(const Object &object, HTTPResponse *rsp);

  // Global store with knowledge base.
  Store *commons_;

  // Knowledge service for searching knowledge base.
  KnowledgeService *kb_;

  // Default types.
  Handles default_types_;

  // Fact catalog for type checking.
  FactCatalog facts_;

  // Symbols.
  Names names_;
  Name n_name_{names_, "name"};
  Name n_description_{names_, "description"};
  Name n_id_space_{names_, "identifierSpace"};
  Name n_schema_space_{names_, "schemaSpace"};
  Name n_default_types_{names_, "defaultTypes"};
  Name n_view_{names_, "view"};
  Name n_preview_{names_, "preview"};
  Name n_suggest_{names_, "suggest"};
  Name n_service_url_{names_, "service_url"};
  Name n_service_path_{names_, "service_path"};
  Name n_extend_{names_, "extend"};
  Name n_versions_{names_, "versions"};
  Name n_url_{names_, "url"};
  Name n_width_{names_, "width"};
  Name n_height_{names_, "height"};
  Name n_query_{names_, "query"};
  Name n_type_{names_, "type"};
  Name n_limit_{names_, "limit"};
  Name n_result_{names_, "result"};
  Name n_score_{names_, "score"};
  Name n_match_{names_, "match"};
  Name n_ids_{names_, "ids"};
  Name n_pid_{names_, "pid"};
  Name n_v_{names_, "v"};
  Name n_properties_{names_, "properties"};
  Name n_meta_{names_, "meta"};
  Name n_rows_{names_, "rows"};
  Name n_int_{names_, "int"};
  Name n_float_{names_, "float"};
  Name n_str_{names_, "str"};
  Name n_entity_{names_, "Q35120"};
  Name n_subclass_of_{names_, "P279"};
  Name n_properties_for_type_{names_, "P1963"};
  Name n_property_{names_, "/w/property"};
};

}  // namespace nlp
}  // namespace sling

#endif  // NLP_KB_REFINE_SERVICE_H_


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

// Sitemap Service

#ifndef NLP_KB_SITEMAP_SERVICE_H_
#define NLP_KB_SITEMAP_SERVICE_H_

#include "sling/frame/store.h"
#include "sling/net/http-server.h"
#include "sling/nlp/kb/knowledge-service.h"

namespace sling {
namespace nlp {

// Sitemap service for cases.
class SitemapService {
 public:
  SitemapService(Store *commons, KnowledgeService *kb);

  // Register sitemap endpoints.
  void Register(HTTPServer *http);

  // Sitemap request handlers.
  void HandleSitemap(HTTPRequest *req, HTTPResponse *rsp);

  // Sitemap index request handlers.
  void HandleSitemapIndex(HTTPRequest *req, HTTPResponse *rsp);

 private:
  // Global store with knowledge base.
  Store *commons_;

  // Knowledge service for searching knowledge base.
  KnowledgeService *kb_;

  // Symbols.
  Names names_;
  Name n_name_{names_, "name"};
  Name n_has_part_{names_, "P527"};
  Name n_main_subject_{names_, "P921"};
  Name n_pub_date_{names_, "P577"};
};

}  // namespace nlp
}  // namespace sling

#endif  // NLP_KB_SITEMAP_SERVICE_H_

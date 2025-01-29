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

#include "sling/nlp/kb/sitemap-service.h"

#include "sling/net/web-service.h"
#include "sling/string/text.h"

namespace sling {
namespace nlp {

SitemapService::SitemapService(Store *commons, KnowledgeService *kb)
    : commons_(commons), kb_(kb) {
  // Bind names.
  names_.Bind(commons);
}

void SitemapService::Register(HTTPServer *http) {
  http->Register("/kb/sitemap", this, &SitemapService::HandleSitemap);
}

void SitemapService::HandleSitemap(HTTPRequest *req, HTTPResponse *rsp) {
  WebService ws(commons_, req, rsp);
  Text itemid = ws.Get("id");
  if (itemid.empty()) {
      rsp->SendError(400, "Bad Request", nullptr);
      return;
  }
  LOG(INFO) << "Sitemap for " << itemid;

  Handle handle = kb_->RetrieveItem(ws.store(), itemid);
  if (handle.IsNil()) {
    rsp->SendError(404, nullptr, "Item not found");
    return;
  }

  Frame item(ws.store(), handle);
  if (!item.valid()) {
    rsp->SendError(400, nullptr, "Invalid item");
    return;
  }

  rsp->set_content_type("text/xml");
  rsp->Append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
  rsp->Append("<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">");
  rsp->Append("\n");
  for (const Slot &s : item.Slots(n_has_part_)) {
    Text partid = ws.store()->FrameId(s.value);
    if (partid.empty()) continue;
    rsp->Append("<url><loc>https://ringgaard.com/kb/");
    rsp->Append(partid);
    rsp->Append("</loc></url>\n");
  }
  rsp->Append("</urlset>\n");
}

}  // namespace nlp
}  // namespace sling

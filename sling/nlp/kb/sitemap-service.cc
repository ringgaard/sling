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
  http->Register("/kb/siteindex", this, &SitemapService::HandleSitemapIndex);
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

void SitemapService::HandleSitemapIndex(HTTPRequest *req, HTTPResponse *rsp) {
  WebService ws(commons_, req, rsp);
  Text indexid = ws.Get("id");
  if (indexid.empty()) {
      rsp->SendError(400, "Bad Request", nullptr);
      return;
  }
  LOG(INFO) << "Sitemap index for " << indexid;

  Handle handle = kb_->RetrieveItem(ws.store(), indexid);
  if (handle.IsNil()) {
    rsp->SendError(404, nullptr, "Index item not found");
    return;
  }

  Frame item(ws.store(), handle);
  if (!item.valid()) {
    rsp->SendError(400, nullptr, "Invalid index item");
    return;
  }

  Frame sitemaps(ws.store(), item.GetHandle(n_main_subject_));
  if (!sitemaps.valid()) {
    rsp->SendError(400, nullptr, "Missing main subject");
    return;
  }

  rsp->set_content_type("text/xml");
  rsp->Append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
  rsp->Append("<sitemapindex ");
  rsp->Append("xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n");

  for (const Slot &s : sitemaps.Slots(n_has_part_)) {
    Frame section(ws.store(), s.value);
    Text section_name = section.GetText(n_name_);
    rsp->Append("\n<!-- ");
    rsp->Append(section_name);
    rsp->Append("-->\n\n");

    for (const Slot &ss : section.Slots(n_has_part_)) {
      Frame part(ws.store(), ss.value);
      Text partid = part.Id();
      if (partid.empty()) continue;
      Text partname = part.GetText(n_name_);
      int pubdate = part.GetInt(n_pub_date_);
      rsp->Append("<sitemap>\n");
      if (!partname.empty()) {
        rsp->Append("  <!-- ");
        rsp->Append(partname);
        rsp->Append("-->\n");
      }
      rsp->Append("  <loc>https://ringgaard.com/kb/");
      rsp->Append(partid);
      rsp->Append("</loc>\n");
      if (pubdate != 0) {
        string date = std::to_string(pubdate);
        date.insert(4, 1, '-');
        date.insert(7, 1, '-');
        rsp->Append("  <lastmod>");
        rsp->Append(date);
        rsp->Append("</lastmod>\n");
      }
      rsp->Append("</sitemap>\n");
    }
  }

  rsp->Append("\n</sitemapindex>\n");
}


}  // namespace nlp
}  // namespace sling

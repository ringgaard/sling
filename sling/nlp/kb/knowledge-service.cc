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

#include "sling/nlp/kb/knowledge-service.h"

#include <math.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "sling/base/flags.h"
#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/frame/object.h"
#include "sling/frame/serialization.h"
#include "sling/frame/store.h"
#include "sling/net/http-server.h"
#include "sling/net/web-service.h"
#include "sling/nlp/kb/calendar.h"
#include "sling/nlp/kb/properties.h"
#include "sling/string/text.h"
#include "sling/string/strcat.h"
#include "sling/util/md5.h"
#include "sling/util/sortmap.h"

DEFINE_string(thumbnails, "", "Thumbnail web service");

namespace sling {
namespace nlp {

// HTML header and footer for landing page.
static const char *html_landing_header =
R"""(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name=viewport content="width=device-width, initial-scale=1">
<link rel="icon" href="/common/image/appicon.ico" type="image/x-icon" />
)""";

static const char *html_landing_footer =
R"""(<script type="module" src="/kb/app/kb.js"></script>
</head>
<body style="display: none;">
</body>
</html>
)""";

// Convert geo coordinate from decimal to minutes and seconds.
static string ConvertGeoCoord(double coord, bool latitude) {
  // Compute direction.
  const char *sign;
  if (coord < 0) {
    coord = -coord;
    sign = latitude ? "S" : "W";
  } else {
    sign = latitude ? "N" : "E";
  }

  // Compute degrees.
  double integer;
  double remainder = modf(coord, &integer);
  int degrees = static_cast<int>(integer);

  // Compute minutes.
  remainder = modf(remainder * 60, &integer);
  int minutes = static_cast<int>(integer);

  // Compute seconds.
  remainder = modf(remainder * 60, &integer);
  int seconds = static_cast<int>(integer + 0.5);

  // Build coordinate string.
  return StrCat(degrees, "°", minutes, "′", seconds, "″", sign);
}

// Make Wikimedia Commons url for file.
static string CommonsUrl(Text filename) {
  // Replace spaces with underscores.
  string fn = filename.str();
  for (char &c : fn) {
    if (c == ' ') c = '_';
  }

  // Compute MD5 digest for filename.
  unsigned char digest[16];
  MD5Digest(digest, fn.data(), fn.size());
  char d1 = "0123456789abcdef"[digest[0] >> 4];
  char d2 = "0123456789abcdef"[digest[0] & 0x0f];

  // Commons files are stored in subdirectories based on the MD5 digest of the
  // filename.
  string url = "https://upload.wikimedia.org/wikipedia/commons/";
  url.push_back(d1);
  url.push_back('/');
  url.push_back(d1);
  url.push_back(d2);
  url.push_back('/');
  for (char c : fn) {
    switch (c) {
      case '?': url.append("%3F"); break;
      case '+': url.append("%2B"); break;
      case '&': url.append("%26"); break;
      default: url.push_back(c);
    }
  }

  return url;
}

// Return thumbnail url for media.
static string Thumbnail(Text url) {
  if (FLAGS_thumbnails.empty()) return url.str();
  string thumb = FLAGS_thumbnails;
  for (char c : url) {
    switch (c) {
      case '?': thumb.append("%3F"); break;
      case '+': thumb.append("%2B"); break;
      case '&': thumb.append("%26"); break;
      case '/': thumb.append("%2F"); break;
      default: thumb.push_back(c);
    }
  }
  return thumb;
}

// Add meta tag to output.
static void AddMeta(HTTPResponse *response,
                    const char *property,
                    const char *name,
                    Text value) {
  response->Append("<meta");

  if (property) {
    response->Append(" property=\"");
    response->Append(property);
    response->Append("\"");
  }

  if (name) {
    response->Append(" name=\"");
    response->Append(name);
    response->Append("\"");
  }

  response->Append(" content=\"");
  response->Append(HTMLEscape(value));
  response->Append("\" />\n");
}

void KnowledgeService::Load(Store *kb, const string &name_table) {
  // Bind names.
  kb_ = kb;
  CHECK(names_.Bind(kb_));
  docnames_ = new DocumentNames(kb);

  // Get meta data for properties.
  std::vector<PropName> xref_properties;
  for (const Slot &s : Frame(kb, kb->Lookup("/w/entity"))) {
    if (s.name != n_role_) continue;
    Frame property(kb, s.value);
    Property p;

    // Get property id and name.
    p.id = s.value;
    p.name = property.GetHandle(n_name_);

    // Property data type.
    p.datatype = property.GetHandle(n_target_);

    // Mark origin data types.
    p.origin = (property.handle() == n_date_of_birth_);

    // Collect xref properties.
    if (p.datatype == n_xref_type_) {
      Text name = kb->GetString(p.name)->str();
      xref_properties.emplace_back(name, p.id);
    }

    p.image = false;
    for (const Slot &ps : property) {
      // Get URL formatter for property.
      if (ps.name == n_formatter_url_ && p.url.empty()) {
        Handle formatter = ps.value;
        bool ignore = false;
        if (kb->IsFrame(formatter)) {
          // Resolve qualified formatter url.
          Frame fq(kb, formatter);
          formatter = fq.GetHandle(Handle::is());

          // Skip deprecated and special services.
          if (fq.Has(n_reason_for_deprecation_)) ignore = true;
          if (fq.Has(n_applies_if_regex_matches_)) ignore = true;
        }
        if (!ignore && kb->IsString(formatter)) {
          p.url = String(kb, formatter).value();
        }
      }

      // Check if property is a representative image for the item.
      if (ps.name == n_instance_of_ && ps.value == n_representative_image_) {
        p.image = true;
      }

      // Collect property usage statistics.
      if (ps.name == n_usage_) {
        float sum = 0.0;
        for (const Slot &u : Frame(kb, ps.value)) {
          float value = u.value.AsInt();
          if (!u.name.IsNil()) p.usage[u.name] = value;
          sum += value;
        }
        if (sum == 0.0) sum = 1.0;
        for (auto &it : p.usage) {
          it.second = it.second / sum * 100.0 + 1.0;
        }
      }
    }

    // Add property.
    properties_[p.id] = p;

    // Add inverse property item.
    Handle inverse = property.GetHandle(n_inverse_label_item_);
    if (!inverse.IsNil()) {
      Frame inverse_property(kb, inverse);
      Property ip;
      ip.id = inverse;
      ip.name = inverse_property.GetHandle(n_name_);
      ip.datatype = n_item_type_.handle();
      ip.image = false;
      properties_[ip.id] = ip;
    }
  }

  // Order xref properties in alphabetical order.
  std::sort(xref_properties.begin(), xref_properties.end());

  // Set up property order.
  int order = 0;
  for (const char **p = property_order; *p != nullptr; ++p) {
    auto f = properties_.find(kb->Lookup(*p));
    if (f != properties_.end()) {
      f->second.order = order++;
    } else {
      VLOG(1) << "Property not know: " << *p;
    }
  }
  for (auto &pn : xref_properties) {
    auto f = properties_.find(pn.id);
    CHECK(f != properties_.end());
    if (f->second.order == kint32max) {
      f->second.order = order++;
    }
  }

  // Initialize calendar.
  calendar_.Init(kb);

  // Load name table.
  if (!name_table.empty()) {
    LOG(INFO) << "Loading name table from " << name_table;
    aliases_.Load(name_table);
  }
}

void KnowledgeService::LoadXref(const string &xref_table) {
  xref_.Load(xref_table);
}

void KnowledgeService::LoadSearchIndex(const string &search_index) {
  search_.Load(search_index);
}

void KnowledgeService::OpenItems(const string &filename) {
  delete items_;
  RecordFileOptions options;
  items_ = new RecordDatabase(filename, options);
}

void KnowledgeService::OpenItemDatabase(const string &db) {
  delete itemdb_;
  itemdb_ = new DBClient();
  CHECK(itemdb_->Connect(db, "kb"));
}

void KnowledgeService::Register(HTTPServer *http) {
  http->Register("/kb", this, &KnowledgeService::HandleLandingPage);
  http->Register("/kb/query", this, &KnowledgeService::HandleQuery);
  http->Register("/kb/search", this, &KnowledgeService::HandleSearch);
  http->Register("/kb/item", this, &KnowledgeService::HandleGetItem);
  http->Register("/kb/frame", this, &KnowledgeService::HandleGetFrame);
  http->Register("/kb/topic", this, &KnowledgeService::HandleGetTopic);
  http->Register("/kb/stubs", this, &KnowledgeService::HandleGetStubs);
  http->Register("/kb/topics", this, &KnowledgeService::HandleGetTopics);
  common_.Register(http);
  app_.Register(http);
}

Handle KnowledgeService::RetrieveItem(Store *store, Text id,
                                      bool offline) const {
  // Look up item in knowledge base.
  Handle handle = store->LookupExisting(id);
  if (!handle.IsNil() && store->IsProxy(handle)) handle = Handle::nil();

  string key = id.str();
  if (handle.IsNil() and xref_.loaded()) {
    // Try looking up in cross-reference.
    if (xref_.Map(&key)) {
      handle = store->LookupExisting(key);
    }
  }

  if (handle.IsNil() && offline && items_ != nullptr) {
    // Try looking up item in the offline item records.
    MutexLock lock(&mu_);
    Record rec;
    if (items_->Lookup(key, &rec)) {
      ArrayInputStream stream(rec.value);
      InputParser parser(store, &stream);
      handle = parser.Read().handle();
    }
  }

  if (handle.IsNil() && offline && itemdb_ != nullptr) {
    // Try looking up item in the offline item database.
    MutexLock lock(&mu_);
    DBRecord rec;
    Status st = itemdb_->Get(key, &rec);
    if (st.ok() && !rec.value.empty()) {
      ArrayInputStream stream(rec.value);
      InputParser parser(store, &stream);
      handle = parser.Read().handle();
    }
  }

  return handle;
}

void KnowledgeService::Preload(const Frame &item, Store *store) {
  // Skip preloading if there is no item database.
  if (itemdb_ == nullptr) return;

  // Find proxies.
  HandleSet proxies;
  item.TraverseSlots([store, &proxies](Slot *s) {
    if (store->IsProxy(s->value)) {
      proxies.insert(s->value);
    }
  });

  // Prefetch items for proxies into store.
  if (!proxies.empty()) {
    std::vector<Slice> keys;
    for (Handle h : proxies) {
      keys.push_back(store->FrameId(h).slice());
    }

    MutexLock lock(&mu_);
    std::vector<DBRecord> recs;
    Status st = itemdb_->Get(keys, &recs);
    if (st.ok()) {
      for (auto &rec : recs) {
        ArrayInputStream stream(rec.value);
        InputParser parser(store, &stream);
        parser.Read();
      }
    } else {
      LOG(WARNING) << "Error fetching items: " << st;
    }
  }
}

void KnowledgeService::HandleLandingPage(HTTPRequest *request,
                                         HTTPResponse *response) {
  // Get item id.
  string itemid;
  if (*request->path() != 0) {
    if (!DecodeURLComponent(request->path() + 1, &itemid)) {
      response->SendError(400, "Bad Request", nullptr);
      return;
    }
  }

  // Send header.
  response->set_content_type("text/html");
  response->Append(html_landing_header);

  // Add social media tags.
  if (itemid.empty()) {
    response->Append("<title>SLING Knowledge base</title>");
  } else {
    // Look up item in knowledge base.
    Store store(kb_);
    Handle handle = RetrieveItem(&store, itemid);

    // Add social media meta tags.
    if (!handle.IsNil()) {
      // Get name, description, and image.
      Frame item(&store, handle);
      Text id = item.Id();
      Text name = item.GetText(n_name_);
      Text description = item.GetText(n_description_);
      Handle image = item.Resolve(n_image_);

      // Add page title.
      if (!name.empty()) {
        response->Append("<title>");
        response->Append(HTMLEscape(name));
        response->Append("</title>\n");
      }

      // Add item id.
      if (!id.empty()) {
        AddMeta(response, "itemid", nullptr, id);
      }

      // Add meta tags for Twitter card and Facebook Open Graph.
      AddMeta(response, nullptr, "twitter:card", "summary");
      AddMeta(response, "og:type", nullptr, "article");
      if (!name.empty()) {
        AddMeta(response, "og:title", "twitter:title", name);
      }
      if (!description.empty()) {
        AddMeta(response, "og:description", "twitter:description", description);
      }
      if (store.IsString(image)) {
        Text filename = store.GetString(image)->str();
        string url = Thumbnail(CommonsUrl(filename));
        AddMeta(response, "og:image", "twitter:image", url);
      }
    }
  }

  // Send remaining header and body.
  response->Append(html_landing_footer);
}

void KnowledgeService::HandleQuery(HTTPRequest *request,
                                   HTTPResponse *response) {
  WebService ws(kb_, request, response);
  Store *store = ws.store();

  // Get query
  Text query = ws.Get("q");
  Text prop = ws.Get("prop");
  bool fullmatch = ws.Get("fullmatch", false);
  int window = ws.Get("window", 5000);
  int limit = ws.Get("limit", 50);
  int boost = ws.Get("boost", 1000);
  VLOG(1) << "Name query: " << query;

  // Lookup name in name table.
  NameTable::Matches matches;
  if (!query.empty()) {
    aliases_.Lookup(query, !fullmatch, window, boost, &matches);
  }

  // Check for exact match with id.
  Handles results(store);
  Handle idmatch = RetrieveItem(store, query, fullmatch);
  if (!idmatch.IsNil()) {
    Frame item(store, idmatch);
    if (item.valid()) {
      Builder match(store);
      GetStandardProperties(item, &match, true);
      results.push_back(match.Create().handle());
    }
  }

  // Get property for type ranking.
  Property *property = nullptr;
  if (!prop.empty()) property = GetProperty(kb_->LookupExisting(prop));

  // Generate response.
  Builder b(store);
  if (property == nullptr) {
    for (const auto &m : matches) {
      if (results.size() >= limit) break;
      Text id = m.second->id();
      Frame item(store, RetrieveItem(store, id));
      if (item.invalid()) continue;
      Builder match(store);
      GetStandardProperties(item, &match, true);
      results.push_back(match.Create().handle());
    }
  } else {
    // Rerank results by type.
    Ranking ranking(limit);
    float specificity = ws.Get("specificity", 1000);
    for (const auto &m : matches) {
      // Get id and score.
      float score = m.first;
      Text id = m.second->id();

      // Retrieve item.
      Handle item = RetrieveItem(store, id);
      if (item.IsNil()) continue;

      // Boost score.
      FrameDatum *f = store->GetFrame(item);
      float boost = 0.0;
      for (const Slot *s = f->begin(); s < f->end(); ++s) {
        if (s->name != n_instance_of_.handle()) continue;
        Handle type = store->Resolve(s->value);
        auto f = property->usage.find(type);
        if (f != property->usage.end()) boost += f->second;
      }
      if (boost != 0.0)  score *= boost * specificity;

      ranking.push(Hit(score, item));
    }
    ranking.sort();

    // Output top results.
    for (const auto &hit : ranking) {
      Frame item(store, hit.item);
      Builder match(store);
      GetStandardProperties(item, &match, true);
      match.Add(n_score_, hit.score);
      results.push_back(match.Create().handle());
    }
  }
  b.Add(n_matches_,  Array(ws.store(), results));

  // Return response.
  ws.set_output(b.Create());
}

void KnowledgeService::HandleSearch(HTTPRequest *request,
                                    HTTPResponse *response) {
  WebService ws(kb_, request, response);

  // Get query
  Text query = ws.Get("q");
  int limit = ws.Get("limit", 50);
  VLOG(1) << "Search query: " << query;

  // Search index.
  SearchEngine::Results results(limit);
  int hits = search_.Search(query, &results);

  // Generate response.
  Handles matches(ws.store());
  std::vector<std::pair<int, Handle>> ranking;
  Builder b(ws.store());
  b.Add(n_hits_, hits);
  for (auto *result : results.hits()) {
    Frame item(ws.store(), RetrieveItem(ws.store(), result->id()));
    if (item.invalid()) continue;
    Builder match(ws.store());
    GetStandardProperties(item, &match, true);
    int count = result->count();
    Text name = item.GetText(n_name_);
    int score = results.Score(name, count);
    match.Add(n_count_, count);
    match.Add(n_score_, score);
    Handle h = match.Create().handle();
    matches.push_back(h);
    ranking.emplace_back(score, h);
  }

  // Sort results by score.
  std::sort(ranking.begin(), ranking.end(),
    [](const std::pair<int, Handle> &a, const std::pair<int, Handle> &b) {
      return a.first > b.first;
    }
  );
  for (int i = 0; i < ranking.size(); ++i) {
    matches[i] = ranking[i].second;
  }
  b.Add(n_matches_,  Array(ws.store(), matches));

  // Return response.
  ws.set_output(b.Create());
}

void KnowledgeService::HandleGetItem(HTTPRequest *request,
                                     HTTPResponse *response) {
  WebService ws(kb_, request, response);

  // Look up item in knowledge base.
  Text itemid = ws.Get("id");
  VLOG(1) << "Look up item '" << itemid << "'";
  Handle handle = RetrieveItem(ws.store(), itemid);
  if (handle.IsNil()) {
    response->SendError(404, nullptr, "Item not found");
    return;
  }

  // Generate response.
  Frame item(ws.store(), handle);
  if (!item.valid()) {
    response->SendError(404, nullptr, "Invalid item");
    return;
  }
  Builder b(ws.store());
  GetStandardProperties(item, &b, true);
  Handle datatype = item.GetHandle(n_target_);
  if (!datatype.IsNil()) {
    Frame dt(ws.store(), datatype);
    if (dt.valid()) {
      b.Add(n_type_, dt.GetHandle(n_name_));
    }
  }

  // Pre-load offline proxies.
  Preload(item, ws.store());

  // Compute start and end for age computations.
  Item info(ws.store());
  Object birth(ws.store(), item.Resolve(n_date_of_birth_));
  if (!birth.IsNil()) info.start.Init(birth);
  Object death(ws.store(), item.Resolve(n_date_of_death_));
  if (!death.IsNil()) info.end.Init(death);

  // Fetch properties.
  FetchProperties(item, &info);
  b.Add(n_properties_, Array(ws.store(), info.properties));
  b.Add(n_xrefs_, Array(ws.store(), info.xrefs));
  b.Add(n_categories_, Array(ws.store(), info.categories));
  b.Add(n_gallery_, Array(ws.store(), info.gallery));

  // Add summary.
  if (item.Has(n_lex_)) {
    // Add document URL.
    Text url = item.GetText(n_url_);
    if (!url.empty()) b.Add(n_url_, url);

    // Add document text.
    Document document(ws.store(), docnames_);
    if (lexer_.Lex(&document, item.GetText(n_lex_))) {
      b.Add(n_document_, ToHTML(document));
    }
  }

  // Return response.
  ws.set_output(b.Create());
}

void KnowledgeService::FetchProperties(const Frame &item, Item *info) {
  // Collect properties and values.
  typedef SortableMap<Property *, Handles *> GroupMap;
  GroupMap property_groups;
  std::vector<Handle> external_media;
  std::unordered_set<string> media_urls;
  Store *store = item.store();
  for (const Slot &s : item) {
    // Collect categories.
    if (s.name == n_category_) {
      Builder b(store);
      Frame cat(store, s.value);
      GetStandardProperties(cat, &b, false);
      info->categories.push_back(b.Create().handle());
      continue;
    }

    // Collect media files.
    if (s.name == n_media_) {
      external_media.push_back(s.value);
    }

    // Look up property. Skip non-property slots.
    Property *property = GetProperty(s.name);
    if (!property) continue;

    // Get property list for property.
    Handles *&property_list = property_groups[property];
    if (property_list == nullptr) {
      property_list = new Handles(store);
    }

    // Add property value.
    property_list->push_back(s.value);
  }

  // Sort properties in display order.
  property_groups.sort([](const GroupMap::Node *n1, const GroupMap::Node *n2) {
    return n1->first->order < n2->first->order;
  });

  // Build property lists.
  for (auto &group : property_groups.array) {
    Property *property = group->first;

    // Add property information.
    Builder p(store);
    p.Add(n_property_, property->name);
    p.Add(n_ref_, property->id);
    p.Add(n_type_, property->datatype);

    // Add property values.
    if (!property->image) {
      SortChronologically(store, group->second);
    }
    Handles values(store);
    for (Handle h : *group->second) {
      // Resolve value.
      Handle value = store->Resolve(h);
      bool qualified = value != h;

      // Add property value based on property type.
      Builder v(store);
      if (property->datatype == n_item_type_) {
        if (store->IsFrame(value)) {
          // Add reference to other item.
          Frame ref(store, value);
          GetStandardProperties(ref, &v, false);
        } else {
          v.Add(n_text_, value);
        }
      } else if (property->datatype == n_xref_type_) {
        // Add external reference.
        String identifier(store, value);
        v.Add(n_text_, identifier);
      } else if (property->datatype == n_property_type_) {
        // Add reference to property.
        Frame ref(store, value);
        if (ref.valid()) {
          GetStandardProperties(ref, &v, false);
        }
      } else if (property->datatype == n_string_type_) {
        // Add string value.
        v.Add(n_text_, value);
      } else if (property->datatype == n_text_type_) {
        // Add text value with language.
        if (store->IsString(value)) {
          String monotext(store, value);
          Handle qual = monotext.qualifier();
          if (qual.IsNil()) {
            v.Add(n_text_, value);
          } else {
            v.Add(n_text_, monotext.text());
            Frame lang(store, qual);
            if (lang.valid()) {
              v.Add(n_lang_, lang.GetHandle(n_name_));
            }
          }
        } else if (store->IsFrame(value)) {
          Frame monotext(store, value);
          v.Add(n_text_, monotext.GetHandle(Handle::is()));
          Frame lang = monotext.GetFrame(n_lang_);
          if (lang.valid()) {
            v.Add(n_lang_, lang.GetHandle(n_name_));
          }
        } else {
          v.Add(n_text_, value);
        }
      } else if (property->datatype == n_url_type_) {
        // Add URL value.
        v.Add(n_text_, value);
        v.Add(n_url_, value);
      } else if (property->datatype == n_media_type_) {
        // Add image.
        v.Add(n_text_, value);
      } else if (property->datatype == n_geo_type_) {
        // Add coordinate value.
        Frame coord(store, value);
        double lat = coord.GetFloat(n_lat_);
        double lng = coord.GetFloat(n_lng_);
        v.Add(n_text_, StrCat(ConvertGeoCoord(lat, true), ", ",
                              ConvertGeoCoord(lng, false)));
        v.Add(n_url_, StrCat("http://maps.google.com/maps?q=",
                              lat, ",", lng));
      } else if (property->datatype == n_quantity_type_) {
        // Add quantity value.
        string text;
        if (store->IsFrame(value)) {
          Frame quantity(store, value);
          text = AsText(store, quantity.GetHandle(n_amount_));

          // Get unit symbol, preferably in latin script.
          Frame unit = quantity.GetFrame(n_unit_);
          text.append(" ");
          text.append(UnitName(unit));
        } else {
          text = AsText(store, value);
        }
        v.Add(n_text_, text);
      } else if (property->datatype == n_time_type_) {
        // Add time value.
        Object time(store, value);
        v.Add(n_text_, calendar_.DateAsString(time));
        if (info->start.precision != Date::NONE) {
          if (property->origin) {
            if (info->end.precision == Date::NONE) {
              int age = info->start.Difference(Date::Today());
              v.Add(n_age_, age);
            }
          } else {
            int years = info->start.Difference(Date(time));
            v.Add(n_age_, years);
          }
        }
      } else if (property->datatype == n_lexeme_type_) {
        if (store->IsFrame(value)) {
          // Add reference to other item.
          Frame ref(store, value);
          GetStandardProperties(ref, &v, false);
        } else {
          v.Add(n_text_, value);
        }
      }

      // Add URL if property has URL formatter.
      if (!property->url.empty() && store->IsString(value)) {
        String identifier(store, value);
        string url = property->url;
        int pos = url.find("$1");
        if (pos != -1) {
          Text replacement = identifier.text();
          url.replace(pos, 2, replacement.data(), replacement.size());
        }
        if (!url.empty()) v.Add(n_url_, url);
      }

      // Get qualifiers.
      if (qualified) {
        Item qualifiers(store);
        qualifiers.start = info->start;
        qualifiers.end = info->end;
        FetchProperties(Frame(store, h), &qualifiers);
        for (Handle xref : qualifiers.xrefs) {
          // Treat xrefs as properties for qualifiers.
          qualifiers.properties.push_back(xref);
        }
        if (!qualifiers.properties.empty()) {
          v.Add(n_qualifiers_, Array(store, qualifiers.properties));
        }
      }

      values.push_back(v.Create().handle());

      // Collect media files for gallery.
      if (property->image) {
        Text filename = String(store, value).text();
        Builder m(store);
        string url = CommonsUrl(filename);
        media_urls.insert(url);
        m.Add(n_url_, url);
        if (qualified) {
          Frame image(store, h);
          Handle legend = image.GetHandle(n_media_legend_);
          if (!legend.IsNil()) m.Add(n_text_, legend);
        }
        info->gallery.push_back(m.Create().handle());
      }
    }
    p.Add(n_values_, Array(store, values));

    // Add property to property list.
    if (property->datatype == n_xref_type_) {
      info->xrefs.push_back(p.Create().handle());
    } else {
      info->properties.push_back(p.Create().handle());
    }

    delete group->second;
  }

  // Add media to gallery.
  for (Handle media : external_media) {
    string url = store->GetString(store->Resolve(media))->str().str();
    if (url.empty()) continue;
    bool nsfw = false;
    if (url[0] == '!') {
      nsfw = true;
      url.erase(0, 1);
    }

    if (media_urls.count(url) > 0) continue;
    media_urls.insert(url);

    Builder m(store);
    m.Add(n_url_, url);
    if (store->IsFrame(media)) {
      Frame image(store, media);
      Handle legend = image.GetHandle(n_media_legend_);
      if (!legend.IsNil()) m.Add(n_text_, legend);

      Handle quality = image.GetHandle(n_has_quality_);
      if (quality.IsNil()) quality = image.GetHandle(n_statement_subject_of_);
      if (quality == n_not_safe_for_work_) nsfw = true;
    }
    if (nsfw) m.Add(n_nsfw_, true);
    info->gallery.push_back(m.Create().handle());
  }
}

void KnowledgeService::GetStandardProperties(Frame &item,
                                             Builder *builder,
                                             bool full) const {
  // Try to retrieve item from offline storage if it is a proxy.
  if (item.IsProxy()) {
    Store *store = item.store();
    Handle h = RetrieveItem(store, item.Id());
    if (!h.IsNil()) item = Frame(store, h);
  }

  // Get reference.
  builder->Add(n_ref_, item.Id());

  // Get name.
  Handle name = item.GetHandle(n_name_);
  if (!name.IsNil()) {
    builder->Add(n_text_, name);
  } else {
    builder->Add(n_text_, item.Id());
  }

  // Get description.
  if (full) {
    Handle description = item.GetHandle(n_description_);
    if (!description.IsNil()) builder->Add(n_description_, description);
  }
}

bool KnowledgeService::Compare(Store *store, Handle a, Handle b) const {
  if (!store->IsFrame(b)) return true;
  if (!store->IsFrame(a)) return false;

  Frame a_frame(store, a);
  int a_order = GetCanonicalOrder(a_frame);
  bool a_ordered = a_order != kint32max;
  Date a_date = GetCanonicalDate(a_frame);
  bool a_dated = a_date.precision != Date::NONE;

  Frame b_frame(store, b);
  int b_order = GetCanonicalOrder(b_frame);
  bool b_ordered = b_order != kint32max;
  Date b_date = GetCanonicalDate(b_frame);
  bool b_dated = b_date.precision != Date::NONE;

  if (a_ordered && b_ordered) {
    // Compare by series ordinal.
    return a_order < b_order;
  } else if (a_dated || b_dated) {
    // Compare by date.
    if (!b_dated) return true;
    if (!a_dated) return false;
    return a_date < b_date;
  } else {
    return false;
  }
}

void KnowledgeService::SortChronologically(Store *store,
                                           Handles *values) const {
  if (values->size() < 2) return;
  std::stable_sort(values->begin(), values->end(), [&](Handle a, Handle b) {
    return Compare(store, a, b);
  });
}

Date KnowledgeService::GetCanonicalDate(const Frame &frame) const {
  Object start = frame.Get(n_start_time_);
  if (start.valid()) return Date(start);

  Object end = frame.Get(n_end_time_);
  if (end.valid()) {
    // Subtract one from end dates to make them sort before start dates.
    Date end_date(end);
    end_date.day--;
    return end_date;
  }

  Object time = frame.Get(n_point_in_time_);
  if (time.valid()) return Date(time);

  return Date();
}

int64 KnowledgeService::GetCanonicalOrder(const Frame &frame) const {
  Text ordinal = frame.GetText(n_series_ordinal_);
  if (ordinal.empty()) return kint32max;
  int64 number = 0;
  for (char c : ordinal) {
    if (c >= '0' && c <= '9') {
      number = number * 10 + (c - '0');
    } else {
      number = number * 128 + c;
    }
  }
  return number;
}

string KnowledgeService::AsText(Store *store, Handle value) {
  value = store->Resolve(value);
  if (value.IsInt()) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value.AsInt());
    return buf;
  } else if (value.IsFloat()) {
    float number = value.AsFloat();
    char buf[32];
    if (floorf(number) == number) {
      snprintf(buf, sizeof(buf), "%.f", number);
    } else if (number > 0.001) {
      snprintf(buf, sizeof(buf), "%.3f", number);
    } else {
      snprintf(buf, sizeof(buf), "%g", number);
    }
    return buf;
  } else {
    return ToText(store, value);
  }
}

string KnowledgeService::UnitName(const Frame &unit) {
  // Check for valid unit.
  if (!unit.valid()) return "";

  // Find best unit symbol, preferably in latin script.
  Store *store = unit.store();
  Handle best = Handle::nil();
  Handle fallback = Handle::nil();
  for (const Slot &s : unit) {
    if (s.name != n_unit_symbol_) continue;
    Frame symbol(store, s.value);
    if (!symbol.valid()) {
      if (fallback.IsNil()) fallback = s.value;
      continue;
    }

    // Prefer latin script.
    Handle script = symbol.GetHandle(n_writing_system_);
    if (script == n_latin_script_ && best.IsNil()) {
      best = s.value;
    } else {
      // Skip language specific names.
      if (symbol.Has(n_language_) || symbol.Has(n_name_language_)) continue;

      // Fall back to symbols with no script.
      if (script == Handle::nil() && fallback.IsNil()) {
        fallback = s.value;
      }
    }
  }
  if (best.IsNil()) best = fallback;

  // Try to get name of best unit symbol.
  if (!best.IsNil()) {
    Handle unit_name = store->Resolve(best);
    if (store->IsString(unit_name)) {
      return String(store, unit_name).value();
    }
  }

  // Fall back to item name of unit.
  return unit.GetString(n_name_);
}

Frame KnowledgeService::GetTopic(Store *store, Text id) {
  // Look up item frame in knowledge base.
  Handle handle = RetrieveItem(store, id);
  if (handle.IsNil()) return Frame::nil();

  // Collect properties.
  Frame item(store, handle);
  std::vector<Statement> statements;
  std::vector<Handle> media;
  Handles images(store);
  for (const Slot &s : item) {
    // Skip categories.
    if (s.name == n_category_) continue;

    // Collect media.
    if (s.name == n_media_) {
      media.push_back(s.value);
      continue;
    }

    // Look up property. Skip non-property slots.
    Property *property = GetProperty(s.name);
    if (!property) continue;

    // Collect Commons images.
    if (property->image) {
      Handle filename = store->Resolve(s.value);
      String url(store, CommonsUrl(store->GetString(filename)->str()));
      if (filename != s.value) {
        Builder m(store);
        for (const Slot &s : Frame(store, s.value)) {
          if (s.name == Handle::is()) {
            m.AddIs(url);
          } else {
            m.Add(s.name, s.value);
          }
        }
        images.push_back(m.Create().handle());
      } else {
        images.push_back(url.handle());
      }
    }

    // Add statement.
    statements.emplace_back(property, s.value);
  }

  // Sort statements in display order.
  std::stable_sort(statements.begin(), statements.end(),
    [&](const Statement &s1, const Statement &s2) {
      if (s1.first->order == s2.first->order) {
        return Compare(store, s1.second, s2.second);
      } else {
        return s1.first->order < s2.first->order;
      }
    });

  // Build topic frame.
  Builder b(store);
  Text itemid = item.Id();
  if (itemid != id) b.AddId(itemid);
  b.AddId(id);
  Handle name = item.GetHandle(n_name_);
  if (!name.IsNil()) {
    b.Add(n_name_, name);
  }
  Handle description = item.GetHandle(n_description_);
  if (!description.IsNil()) {
    b.Add(n_description_, description);
  }

  // Add statements in display order.
  for (const Statement &s : statements) {
    bool qualified = s.second != store->Resolve(s.second);
    if (qualified) {
      // Sort statement qualifiers.
      Builder q(store);
      q.AddFrom(s.second);
      std::stable_sort(q.begin(), q.end(),
        [&](const Slot &s1, const Slot &s2) {
          if (s1.name == s2.name) {
            return Compare(store, s1.value, s2.value);
          } else {
            Property *p1 = GetProperty(s1.name);
            Property *p2 = GetProperty(s2.name);
            if (!p1) return false;
            if (!p2) return true;
            return p1->order < p2->order;
          }
        });
      b.Add(s.first->id, q.Create());
    } else {
      // Add non-qualified statement.
      b.Add(s.first->id, s.second);
    }
  }

  // Add media.
  for (Handle &m : images) {
    b.Add(n_media_, m);
  }
  for (Handle &m : media) {
    b.Add(n_media_, m);
  }

  return b.Create();
}

void KnowledgeService::HandleGetFrame(HTTPRequest *request,
                                      HTTPResponse *response) {
  WebService ws(kb_, request, response);

  // Look up frame in knowledge base.
  Text id = ws.Get("id");
  Handle handle = RetrieveItem(ws.store(), id);
  if (handle.IsNil()) {
    response->SendError(404, nullptr, "Item not found");
    return;
  }

  // Return frame as response.
  ws.set_output(Object(ws.store(), handle));
}

void KnowledgeService::HandleGetTopic(HTTPRequest *request,
                                      HTTPResponse *response) {
  WebService ws(kb_, request, response);
  Store *store = ws.store();

  // Look up frame in knowledge base.
  Text id = ws.Get("id");
  Frame topic = GetTopic(store, id);
  if (topic.invalid()) {
    response->SendError(404, nullptr, "Topic not found");
    return;
  };

  // Return topic as response.
  ws.set_output(topic);
}

void KnowledgeService::HandleGetStubs(HTTPRequest *request,
                                      HTTPResponse *response) {
  WebService ws(kb_, request, response);
  Store *store = ws.store();

  // Expected input is an array of frames.
  if (!ws.input().IsArray()) {
    response->set_status(400);
    return;
  }
  Array frames = ws.input().AsArray();
  int size = frames.length();

  // Return array of stub frames with id and name.
  IOBufferOutputStream stream(response->buffer());
  Output out(&stream);
  response->set_content_type("application/sling");
  Encoder encoder(store, &out);
  Array stubs(store, size);
  for (int i = 0; i < size; ++i) {
    Frame item(store, frames.get(i));
    if (item.valid() && !item.IsProxy()) {
      Builder b(store);
      b.AddId(item.Id());
      Handle name = item.GetHandle(n_name_);
      if (!name.IsNil()) b.Add(n_name_, name);
      Frame stub = b.Create();
      stubs.set(i, stub.handle());
      encoder.Encode(stub);
    }
  }
  encoder.Encode(stubs);
}

void KnowledgeService::HandleGetTopics(HTTPRequest *request,
                                       HTTPResponse *response) {
  WebService ws(kb_, request, response);
  Store *store = ws.store();

  // Expected input is an array of frames.
  if (!ws.input().IsArray()) {
    response->set_status(400);
    return;
  }
  Array frames = ws.input().AsArray();
  int size = frames.length();

  // Return array of topics.
  IOBufferOutputStream stream(response->buffer());
  Output out(&stream);
  response->set_content_type("application/sling");
  Encoder encoder(store, &out);
  Array topics(store, size);
  for (int i = 0; i < size; ++i) {
    Text id = store->FrameId(frames.get(i));
    Frame topic = GetTopic(store, id);
    if (topic.IsNil()) continue;
    topics.set(i, topic.handle());
    encoder.Encode(topic);
  }
  encoder.Encode(topics);
}

string KnowledgeService::GetImage(const Frame &item) {
  Store *store = item.store();
  for (const Slot &s : item) {
    // Return first media file.
    if (s.name == n_media_) {
      Handle url = store->Resolve(s.value);
      if (store->IsString(url)) {
        return store->GetString(url)->str().str();
      }
    }

    // Look up property.
    Property *property = GetProperty(s.name);
    if (!property || !property->image) continue;
    Handle filename = store->Resolve(s.value);
    if (store->IsString(filename)) {
      return CommonsUrl(store->GetString(filename)->str());
    }
  }

  return "";
}

}  // namespace nlp
}  // namespace sling


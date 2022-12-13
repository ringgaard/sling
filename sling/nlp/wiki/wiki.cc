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

#include "sling/nlp/wiki/wiki.h"

#include <string>

#include "sling/string/strcat.h"
#include "sling/util/unicode.h"

namespace sling {
namespace nlp {

// Language priority order.
const char *Wiki::language_priority[] = {
  "en", "da", "sv", "no", "de", "fr", "es", "it",
  "nl", "pt", "pl", "fi",
  "ca", "eu", "la", "eo", "cs", "sh", "hu", "ro",
  "el", "ru", "uk", "sr", "bg",
  "ms", "sk", "hr", "lt", "lv", "et",
  "id", "af",
  nullptr,
};

// Alias source names.
const char *alias_source_name[NUM_ALIAS_SOURCES] = {
  "generic",
  "wikidata_label",
  "wikidata_alias",
  "wikipedia_title",
  "wikipedia_redirect",
  "wikipedia_anchor",
  "wikipedia_disambiguation",
  "wikidata_foreign",
  "wikidata_native",
  "wikidata_demonym",
  "wikipedia_link",
  "wikidata_name",
  "wikipedia_name",
  "wikipedia_nickname",
  "variation",
  "transfer",
};

void Wiki::SplitTitle(const string &title,
                      string *name,
                      string *disambiguation) {
  // Find last phrase in parentheses.
  size_t open = -1;
  size_t close = -1;
  for (int i = 0; i < title.size(); ++i) {
    if (title[i] == '(') {
      open = i;
      close = -1;
    } else if (title[i] == ')') {
      close = i;
    }
  }

  // Split title into name and disambiguation parts.
  if (open > 1 && close == title.size() - 1) {
    int end = open - 1;
    while (end > 0 && title[end - 1] == ' ') end--;
    name->assign(title, 0, end);
    disambiguation->assign(title, open + 1, close - open - 1);
  } else {
    *name = title;
    disambiguation->clear();
  }
}

string Wiki::Id(Text lang, Text title) {
  string t;
  UTF8::ToTitleCase(title.data(), title.size(), &t);
  for (char &c : t) {
    if (c == ' ') c = '_';
  }
  return StrCat("/wp/", lang, "/", t);
}

string Wiki::Id(Text lang, Text prefix, Text title) {
  string t;
  UTF8::ToTitleCase(title.data(), title.size(), &t);
  for (char &c : t) {
    if (c == ' ') c = '_';
  }
  return StrCat("/wp/", lang, "/", prefix, ":", t);
}

string Wiki::URL(Text lang, Text title) {
  string t;
  UTF8::ToTitleCase(title.data(), title.size(), &t);
  for (char &c : t) {
    if (c == ' ') c = '_';
  }
  return StrCat("https://", lang, ".wikipedia.org/wiki/", t);
}

void WikimediaTypes::Init(Store *store) {
  static const char *category_types[] = {
    "Q4167836",    // Wikimedia category
    "Q13331174",   // Wikimedia navboxes category
    "Q13331174",   // Wikimedia navboxes category
    "Q15407973",   // Wikimedia disambiguation category
    "Q15647814",   // Wikimedia administration category
    "Q20010800",   // Wikimedia user language category
    "Q20769287",   // Wikimedia user category
    "Q23894233",   // Wikimedia templates category
    "Q23894246",   // Wikimedia infobox templates category
    "Q24046192",   // Wikimedia category of stubs
    "Q24046192",   // Wikimedia category of stubs
    "Q24571879",   // category by name in Wikimedia
    "Q30330522",   // Wikimedia unknown parameters category
    "Q30432511",   // metacategory in Wikimedia projects
    "Q56428020",   // Wikimedia lists category
    "Q59541917",   // Wikimedia topic category
    "Q59542487",   // Wikimedia set category
    "Q67131190",   // Wikimedia tracking category
    "Q105653689",  // Editnotice category
    "Q106574913",  // Wikimedia albums-by-language category
    "Q106575300",  // Wikimedia albums-by-genre category
    "Q106612246",  // Wikimedia albums-by-performer category
    nullptr
  };

  for (const char **c = category_types; *c != nullptr; ++c) {
    Handle type = store->Lookup(*c);
    category_types_.insert(type);
  }

  // Initialize biographic item types.
  const char *biographic_item_types[] = {
    "Q273057",     // discography
    "Q1371849",    // filmography
    "Q17438413",   // videography
    "Q1631107",    // bibliography
    "Q1075660",    // artist discography
    "Q59248059",   // singles discography
    "Q20054355",   // career statistics
    "Q59191021",   // Wikimedia albums discography
    "Q104635718",  // Wikimedia artist discography
    "Q59248072",   // Wikimedia EPs discography
    nullptr,
  };

  for (const char **b = biographic_item_types; *b; ++b) {
    Handle type = store->Lookup(*b);
    biographic_types_.insert(type);
  }

  CHECK(names_.Bind(store));
}

bool WikimediaTypes::IsCategory(Handle type) const {
  return category_types_.count(type) > 0;
}

bool WikimediaTypes::IsDisambiguation(Handle type) const {
  return type == n_disambiguation_ || type == n_human_name_disambiguation_;
}

bool WikimediaTypes::IsList(Handle type) const {
  return type == n_list_ ||
         type == n_list_of_characters_ ||
         type == n_music_list_;
}

bool WikimediaTypes::IsTemplate(Handle type) const {
  return type == n_template_ || type == n_navigational_template_;
}

bool WikimediaTypes::IsInfobox(Handle type) const {
  return type == n_infobox_;
}

bool WikimediaTypes::IsDuplicate(Handle type) const {
  return type == n_permanent_duplicate_item_;
}

bool WikimediaTypes::IsNonEntity(Handle type) const {
  return IsCategory(type) ||
         IsDisambiguation(type) ||
         IsList(type) ||
         IsTemplate(type) ||
         IsInfobox(type) ||
         IsDuplicate(type);
}

bool WikimediaTypes::IsBiographic(Handle type) const {
  return biographic_types_.count(type) > 0;
}

void AuxFilter::Init(Store *store) {
  const char *aux_item_types[] = {
    "Q13442814",  // scholarly article
    "Q17329259",  // encyclopedic article
    "Q732577",    // publication

    "Q17633526",  // Wikinews article
    "Q17362920",  // Wikimedia duplicated page

    "Q7187",      // gene
    "Q16521",     // taxon
    "Q8054",      // protein
    "Q11173",     // chemical compound
    "Q30612",     // clinical trial

    "Q523",       // star
    "Q318",       // galaxy
    nullptr,
  };
  for (const char **type = aux_item_types; *type; ++type) {
    aux_types_.insert(store->Lookup(*type));
  }
  names_.Bind(store);
}

bool AuxFilter::IsAux(const Frame &frame) const {
  // Never mark items in Wikipedia as aux.
  Frame wikipedia = frame.GetFrame(n_wikipedia_);
  if (wikipedia.valid() && wikipedia.size() > 0) return false;

  // Check item types.
  for (const Slot &slot : frame) {
    if (slot.name != n_instanceof_) continue;
    Handle type = frame.store()->Resolve(slot.value);
    if (aux_types_.count(type) > 0) return true;
  }

  return false;
}

}  // namespace nlp
}  // namespace sling


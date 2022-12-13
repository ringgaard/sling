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

#ifndef SLING_NLP_WIKI_WIKI_H_
#define SLING_NLP_WIKI_WIKI_H_

#include <string>

#include "sling/base/types.h"
#include "sling/frame/object.h"
#include "sling/frame/store.h"
#include "sling/string/text.h"

namespace sling {
namespace nlp {

// Wikidata object types.
enum WikidataType {
  WIKIDATA_ITEM = 0,
  WIKIDATA_PROPERTY = 1,
  WIKIDATA_LEXEME = 2,
};

// Wikipedia name spaces.
enum WikipediaNamespace {
  WIKIPEDIA_NAMESPACE_MAIN      = 0,
  WIKIPEDIA_NAMESPACE_USER      = 2,
  WIKIPEDIA_NAMESPACE_WIKIPEDIA = 4,
  WIKIPEDIA_NAMESPACE_FILE      = 6,
  WIKIPEDIA_NAMESPACE_MEDIAWIKI = 8,
  WIKIPEDIA_NAMESPACE_TEMPLATE  = 10,
  WIKIPEDIA_NAMESPACE_HELP      = 12,
  WIKIPEDIA_NAMESPACE_CATEGORY  = 14,
};

// Alias sources.
enum AliasSource {
  SRC_GENERIC                  =  0,  //     1  0x0001
  SRC_WIKIDATA_LABEL           =  1,  //     2  0x0002
  SRC_WIKIDATA_ALIAS           =  2,  //     4  0x0004
  SRC_WIKIPEDIA_TITLE          =  3,  //     8  0x0008
  SRC_WIKIPEDIA_REDIRECT       =  4,  //    16  0x0010
  SRC_WIKIPEDIA_ANCHOR         =  5,  //    32  0x0020
  SRC_WIKIPEDIA_DISAMBIGUATION =  6,  //    64  0x0040
  SRC_WIKIDATA_FOREIGN         =  7,  //   128  0x0080
  SRC_WIKIDATA_NATIVE          =  8,  //   256  0x0100
  SRC_WIKIDATA_DEMONYM         =  9,  //   512  0x0200
  SRC_WIKIPEDIA_LINK           = 10,  //  1024  0x0400
  SRC_WIKIDATA_NAME            = 11,  //  2048  0x0800
  SRC_WIKIPEDIA_NAME           = 12,  //  4096  0x1000
  SRC_WIKIPEDIA_NICKNAME       = 13,  //  8192  0x2000
  SRC_VARIATION                = 14,  // 16384  0x4000
  SRC_TRANSFER                 = 15,  // 32768  0x8000
};

static const int NUM_ALIAS_SOURCES = 16;

extern const char *alias_source_name[NUM_ALIAS_SOURCES];

// Utility functions for Wikidata and Wikipedia.
class Wiki {
 public:
  // Split title into name and disambiguation.
  static void SplitTitle(const string &title,
                         string *name,
                         string *disambiguation);

  // Return id for Wikipedia page.
  static string Id(Text lang, Text title);
  static string Id(Text lang, Text prefix, Text title);

  // Return URL for Wikipedia page.
  static string URL(Text lang, Text title);

  // Language priority order.
  static const char *language_priority[];
};

// Wikimedia item types for special Wikimedia pages.
class WikimediaTypes {
 public:
  // Initialize Wikimedia types.
  void Init(Store *store);

  // Check if item is a Wikipedia category.
  bool IsCategory(Handle type) const;

  // Check if item is a Wikipedia disambiguation page.
  bool IsDisambiguation(Handle type) const;

  // Check if item is a Wikipedia list article.
  bool IsList(Handle type) const;

  // Check if item is a Wikipedia template.
  bool IsTemplate(Handle type) const;

  // Check if item is a Wikipedia infobox.
  bool IsInfobox(Handle type) const;

  // Check if item is a duplicate.
  bool IsDuplicate(Handle type) const;

  // Check if item is a non-entity.
  bool IsNonEntity(Handle type) const;

  // Check if item is biographical.
  bool IsBiographic(Handle type) const;

 private:
  // Category types.
  HandleSet category_types_;

  // Biographic types.
  HandleSet biographic_types_;

  // Names.
  Names names_;
  Name n_disambiguation_{names_, "Q4167410"};
  Name n_human_name_disambiguation_{names_, "Q22808320"};
  Name n_list_{names_, "Q13406463"};
  Name n_list_of_characters_{names_, "Q63032896"};
  Name n_music_list_{names_, "Q98645843"};

  Name n_template_{names_, "Q11266439"};
  Name n_navigational_template_{names_, "Q11753321"};
  Name n_infobox_{names_, "Q19887878"};
  Name n_permanent_duplicate_item_{names_, "Q21286738"};
};

// Filter for auxiliary items. The auxiliary items in the knowledge base are
// items that are used infrequently and are stored in a separate knowledge
// base store.
class AuxFilter {
 public:
  // Initialize auxiliary item filter.
  void Init(Store *store);

  // Check if item is an auxiliary item.
  bool IsAux(const Frame &frame) const;

 private:
  // Auxiliary item types.
  HandleSet aux_types_;

  // Names.
  Names names_;
  Name n_wikipedia_{names_, "/w/item/wikipedia"};
  Name n_instanceof_{names_, "P31"};
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_WIKI_WIKI_H_


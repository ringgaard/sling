#ifndef NLP_WIKI_WIKI_H_
#define NLP_WIKI_WIKI_H_

#include <string>

#include "base/types.h"

namespace sling {
namespace nlp {

// Alias sources.
enum AliasSource {
  SRC_GENERIC                  = 0,
  SRC_WIKIDATA_LABEL           = 1,
  SRC_WIKIDATA_ALIAS           = 2,
  SRC_WIKIPEDIA_TITLE          = 3,
  SRC_WIKIPEDIA_REDIRECT       = 4,
  SRC_WIKIPEDIA_ANCHOR         = 5,
  SRC_WIKIPEDIA_DISAMBIGUATION = 6,
};

static const int kNumAliasSources = 7;

extern const char *kAliasSourceName[kNumAliasSources];

// Utility functions for Wikidata and Wikipedia.
class Wiki {
 public:
  // Split title into name and disambiguation.
  static void SplitTitle(const string &title,
                         string *name,
                         string *disambiguation);

  // Return id for Wikipedia page.
  static string Id(const string &lang, const string &title);
  static string Id(const string &lang,
                   const string &prefix,
                   const string &title);

  // Return URL for Wikipedia page.
  static string URL(const string &lang, const string &title);

  // Language priority order.
  static const char *language_priority[];
};

}  // namespace nlp
}  // namespace sling

#endif  // NLP_WIKI_WIKI_H_

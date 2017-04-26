#include "nlp/wiki/wiki.h"

#include <string>

#include "util/unicode.h"

namespace sling {
namespace nlp {

// Language priority order.
const char *Wiki::language_priority[] = {
  "en", "da", "sv", "no", "de", "fr", "es", "it", "nl", "pt", "pl", "fi",
  nullptr,
};

// Alias source names.
const char *kAliasSourceName[kNumAliasSources] = {
  "generic",
  "wikidata_label",
  "wikidata_alias",
  "wikipedia_title",
  "wikipedia_redirect",
  "wikipedia_achor",
  "wikipedia_disambiguation",
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
    while (end > 0 and title[end - 1] == ' ') end--;
    name->assign(title, 0, end);
    disambiguation->assign(title, open + 1, close - open - 1);
  } else {
    *name = title;
    disambiguation->clear();
  }
}

string Wiki::Id(const string &lang, const string &title) {
  string t;
  UTF8::ToTitleCase(title, &t);
  for (char &c : t) {
    if (c == ' ') c = '_';
  }
  return "/wp/" + lang + "/" + t;
}

string Wiki::Id(const string &lang, const string &prefix, const string &title) {
  string t;
  UTF8::ToTitleCase(title, &t);
  for (char &c : t) {
    if (c == ' ') c = '_';
  }
  return "/wp/" + lang + "/" + prefix + ":" + t;
}

string Wiki::URL(const string &lang, const string &title) {
  string t;
  UTF8::ToTitleCase(title, &t);
  for (char &c : t) {
    if (c == ' ') c = '_';
  }
  return "http://" + lang + ".wikipedia.org/wiki/" + t;
}

}  // namespace nlp
}  // namespace sling


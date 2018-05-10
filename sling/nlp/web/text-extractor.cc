#include "sling/nlp/web/text-extractor.h"

#include <iostream>
#include <string>

#include "sling/string/ctype.h"
#include "sling/string/strcat.h"
#include "sling/web/html-parser.h"

namespace sling {
namespace nlp {

// Compare two tags.
static bool TagEqual(const char *t1, const char *t2) {
  return strcasecmp(t1, t2) == 0;
}

// Compute fingerprint for tag.
static uint64 TagFingerprint(const char *tag, const char *cls) {
  // Compute fingerprint for tag.
  uint64 fp = Fingerprint(tag, strlen(tag));
  if (cls) {
    // Compute fingerprint for class discarding whitespace and digits.
    const unsigned char *p = reinterpret_cast<const unsigned char *>(cls);
    const unsigned char *end = p + strlen(cls);
    int n = 0;
    uint64 acc = 0;
    while (p < end) {
      unsigned char ch = *p++;
      if (!ascii_isspace(ch) && !ascii_isdigit(ch)) {
        acc = (acc << 8) | ch;
        if (++n == 8) {
          fp = FingerprintCat(fp, acc);
          acc = 0;
          n = 0;
        }
      }
    }
    if (n > 0) fp = FingerprintCat(fp, acc);
  }

  return fp;
}

// Compute fingerprint for element.
static uint64 TagFingerprint(const XMLElement &e) {
  const char *cls = e.Get("class");
  if (cls == nullptr) cls = e.Get("id");
  return TagFingerprint(e.name, cls);
}

// Get tag identifier.
static void GetTagIdentifier(const XMLElement &e, string *ident) {
  ident->append(e.name);
  const char *cls = e.Get("class");
  if (cls) {
    ident->push_back('.');
    ident->append(cls);
  } else {
    const char *id = e.Get("id");
    if (id) {
      ident->push_back('#');
      ident->append(id);
    }
  }
}

bool WebsiteAnalysis::AddPage(const string &url) {
  if (!url.empty()) {
    uint64 fp = Fingerprint(url.data(), url.size());
    if (urls_.count(fp) > 0) return false;
    urls_.insert(fp);
  }
  num_pages_++;
  return true;
}

void WebsiteAnalysis::AddTag(uint64 signature, int score) {
  scores_[signature] += score;
}

void WebsiteAnalysis::PreserveTag(uint64 signature) {
  sticky_.insert(signature);
}

void WebsiteAnalysis::Block(const char *tag, const char *cls) {
  blocked_.insert(TagFingerprint(tag, cls));
}

void WebsiteAnalysis::AddPhrase(const char *phrase, uint64 signature) {
  // Only record phrases with letters.
  int len = strlen(phrase);
  bool letters = false;
  for (const char *p = phrase; *p; ++p) {
    if (ascii_isalpha(*p)) {
      letters = true;
      break;
    }
  }
  if (!letters) return;

  // Compute fingerprint for signature and phrase.
  uint64 fp = FingerprintCat(signature, Fingerprint(phrase, len));

  // Add phrase to phrase table.
  PhraseInfo &p = phrases_[fp];
  p.signature = signature;
  p.count++;
}

void WebsiteAnalysis::Finalize() {
  // Discount repeated phrases in tag scores.
  for (auto &it : phrases_) {
    PhraseInfo &pi = it.second;
    if (pi.count > 3) {
      scores_[pi.signature] -= pi.count;
    }
  }

  // Make sure that all sticky tags are kept.
  for (uint64 fp : sticky_) {
    if (scores_[fp] <= 0) scores_[fp] = 666;
  }
}

int WebsiteAnalysis:: PhraseCount(const char *phrase, uint64 signature) const {
  // Compute fingerprint for signature and phrase.
  int len = strlen(phrase);
  uint64 fp = FingerprintCat(signature, Fingerprint(phrase, len));

  // Lookup phrase.
  auto f = phrases_.find(fp);
  if (f == phrases_.end()) return 0;
  return f->second.count;
}

void WebsiteAnalysis::GetFingerprints(std::vector<uint64> *fingerprints) const {
  for (const auto &it : scores_) {
    if (it.second > 0) fingerprints->push_back(it.first);
  }
}

bool WebPageAnalyzer::StartElement(const XMLElement &e) {
  // Only parse body markup.
  bool is_body = false;
  if (!in_body_) {
    if (TagEqual(e.name, "body")) {
      in_body_ = true;
      is_body = true;
      if (!analysis_->AddPage(url_)) return false;
    } else if (TagEqual(e.name, "meta")) {
      const char *property = e.Get("property");
      const char *content = e.Get("content");
      if (property && content) {
       if (TagEqual(property, "og:url")) {
         url_ = content;
       }
      }
    }
  }
  if (!in_body_) return true;

  // Skip style sheets and scripts.
  if (TagEqual(e.name, "style")) in_style_ = true;
  if (TagEqual(e.name, "script")) in_script_ = true;

  // Get parent tag (or root).
  TagInfo &parent = nesting_.empty() ? root_ : nesting_.back();

  // Compute fingerprint for tag.
  uint64 fp = is_body ? 1 : TagFingerprint(e);

  // Compute fingerprint signature for nested tag.
  uint64 signature = FingerprintCat(parent.signature, fp);

  // Add tag to stack.
  nesting_.emplace_back(signature, parent.text_length);

  // Check if tag has been manually blocked.
  if (analysis_->Blocked(fp)) {
    nesting_.back().blocked = true;
  }

  // Track paragraphs.
  if (paragraph_level_ > 0) {
    paragraph_level_++;
  } else if (TagEqual(e.name, "p") || TagEqual(e.name, "arttextxml")) {
    paragraph_level_++;
  } else if (TagEqual(e.name, "div")) {
    const char *cls = e.Get("class");
    if (cls && strstr(cls, "para")) {
      // Special favor for CNN.
      paragraph_level_++;
    }
  }

  return true;
}

bool WebPageAnalyzer::EndElement(const char *name) {
  // Skip content in header.
  if (!in_body_ || nesting_.empty()) return true;
  TagInfo &taginfo = nesting_.back();

  // Discard style sheets and scripts.
  if (in_style_) {
    taginfo.keep = false;
    if (TagEqual(name, "style")) in_style_ = false;
  }
  if (in_script_) {
    taginfo.keep = false;
    if (TagEqual(name, "script")) in_script_ = false;
  }

  // Track paragraphs.
  if (paragraph_level_ > 0) paragraph_level_--;

  // Discard blocked tags.
  if (taginfo.blocked) {
    taginfo.keep = false;
  } else {
    // Discard non-content tag.
    int text_length = taginfo.text_length;
    if (text_length == 0) taginfo.keep = false;

    // Keep formating tags inside paragraph.
    bool formatting = false;
    if (paragraph_level_ > 0) {
      if (TagEqual(name, "a") ||
          TagEqual(name, "span") ||
          TagEqual(name, "b") ||
          TagEqual(name, "em") ||
          TagEqual(name, "i") ||
          TagEqual(name, "ins") ||
          TagEqual(name, "font") ||
          TagEqual(name, "time") ||
          TagEqual(name, "strong")) {
        formatting = true;
        taginfo.keep = true;
      }
    }

    // Discard tags that are outside the main flow.
    if (TagEqual(name, "noscript") ||
        TagEqual(name, "header") ||
        TagEqual(name, "footer") ||
        TagEqual(name, "figcaption") ||
        TagEqual(name, "figure")) {
      taginfo.keep = false;
    } else {
      // Keep tag if children contain contents.
      if (taginfo.keep_children) {
        taginfo.keep = true;
        analysis_->PreserveTag(taginfo.signature);
      }
    }

    // Keep parent tags for content tags.
    if (taginfo.keep && !formatting) {
      for (TagInfo &ti : nesting_) ti.keep_children = true;
    }
  }

  // Vote on content.
  analysis_->AddTag(taginfo.signature, taginfo.keep ? 1 : -1);

  nesting_.pop_back();
  return true;
}

bool WebPageAnalyzer::Text(const char *str) {
  // Skip text in header.
  if (!in_body_ || nesting_.empty()) return true;

  // Skip style sheets and scripts.
  if (in_style_ || in_script_) return true;

  // Skip whitespace.
  const char *s = str;
  while (ascii_isspace(*s)) s++;
  if (*s == 0) return true;

  // Check for final punctuation.
  int len = strlen(str);
  const char *e = str + len;
  while (e > s && ascii_isspace(e[-1])) e--;
  bool keep = false;
  if (e > s && e[-1] == '.') keep = true;
  if (e > s + 2) {
    // Check if text ends in ... (or ellipsis).
    if (e[-3] == '.' && e[-2] == '.') keep = false;
    if (e[-3] == '\xe2' && e[-2] == '\x80' && e[-1] == '\xa6') keep = false;
  }

  // Discard text with characters that are rarely used in normal text.
  if (keep) {
    if (strchr(str, '#') != nullptr) keep = false;
    if (strchr(str, '@') != nullptr) keep = false;
    if (strchr(str, '|') != nullptr) keep = false;
    if (strchr(str, '{') != nullptr) keep = false;
    if (strchr(str, '}') != nullptr) keep = false;
    if (strchr(str, '\\') != nullptr) keep = false;
    if (strstr(str, "©") != nullptr) keep = false;
    if (strstr(str, "™") != nullptr) keep = false;
  }

  // Update current tag.
  TagInfo &taginfo = nesting_.back();
  taginfo.keep = keep;
  taginfo.text_length += len;

  // Update phrase map.
  analysis_->AddPhrase(str, taginfo.signature);

  return true;
}

bool WebPageTextExtractor::StartElement(const XMLElement &e) {
  // Only parse body markup.
  bool is_body = false;
  if (!in_body_) {
    if (TagEqual(e.name, "body")) {
      in_body_ = true;
      is_body = true;
    } else if (TagEqual(e.name, "meta")) {
      // Get web page meta data.
      const char *property = e.Get("property");
      if (!property) property = e.Get("itemprop");
      if (!property) property = e.Get("name");
      const char *content = e.Get("content");
      if (property && content) {
        if (TagEqual(property, "og:url")) {
          url_ = content;
        } else if (TagEqual(property, "og:title")) {
          title_ = content;
        } else if (TagEqual(property, "og:type")) {
          type_ = content;
        } else if (TagEqual(property, "og:site_name")) {
          site_ = content;
        } else if (TagEqual(property, "og:pubdate") ||
                   TagEqual(property, "og:article:published_time") ||
                   TagEqual(property, "article:published_time") ||
                   TagEqual(property, "rnews:datePublished") ||
                   TagEqual(property, "datePublished") ||
                   TagEqual(property, "dateCreated") ||
                   TagEqual(property, "date") ||
                   TagEqual(property, "sailthru.date") ||
                   TagEqual(property, "parsely-pub-date") ||
                   TagEqual(property, "dc.date")) {
          date_ = content;
        }
      }
    } else if (TagEqual(e.name, "title")) {
      in_title_ = true;
    }
  }
  if (!in_body_) return true;

  // Skip style sheets and scripts.
  if (TagEqual(e.name, "style")) in_style_ = true;
  if (TagEqual(e.name, "script")) in_script_ = true;

  // Get parent tag (or root).
  TagInfo &parent = nesting_.empty() ? root_ : nesting_.back();

  // Compute fingerprint for tag.
  uint64 fp = is_body ? 1 : TagFingerprint(e);

  // Compute fingerprint signature for nested tag.
  uint64 signature = FingerprintCat(parent.signature, fp);

  // Check if we should keep the contents of the tag.
  bool keep = false;
  if (parent.keep) {
    keep = analysis_->Keep(signature);
  }

  // Add tag to stack.
  nesting_.emplace_back(signature, keep);

  // Add tag debug information.
  if (debug_) {
    GetTagIdentifier(e, &nesting_.back().id);
  }

  // Check for paragraph break.
  if (brk_ < PARAGRAPH) {
    if (TagEqual(e.name, "p") ||
        TagEqual(e.name, "div") ||
        TagEqual(e.name, "li")) {
      brk_ = PARAGRAPH;
    }
  }

  return true;
}

bool WebPageTextExtractor::EndElement(const char *name) {
  // End title.
  if (in_title_) {
    if (TagEqual(name, "title")) in_title_ = false;
  }

  // Skip content in header.
  if (!in_body_ || nesting_.empty()) return true;

  // Discard style sheets and scripts.
  if (in_style_) {
    if (TagEqual(name, "style")) in_style_ = false;
  } else if (in_script_) {
    if (TagEqual(name, "script")) in_script_ = false;
  }

  // Check for paragraph break.
  if (brk_ < PARAGRAPH) {
    if (TagEqual(name, "p") ||
        TagEqual(name, "div") ||
        TagEqual(name, "li")) {
      brk_ = PARAGRAPH;
    }
  }
  if (brk_ < NEWLINE) {
    if (TagEqual(name, "br")) brk_ = NEWLINE;
  }

  nesting_.pop_back();

  return true;
}

bool WebPageTextExtractor::Text(const char *str) {
  // Get page title.
  if (in_title_) page_title_.append(str);

  // Skip text in header.
  if (!in_body_ || nesting_.empty()) return true;

  // Skip style sheets and scripts.
  if (in_style_ || in_script_) return true;

  if (debug_) {
    // Output text with debugging information.
    DebugText(str);
  } else {
    // Check if we are inside a tag context with text contents.
    if (nesting_.back().keep) {
      // Extract text.
      const unsigned char *s = reinterpret_cast<const unsigned char *>(str);
      const unsigned char *end = s + strlen(str);
      while (s < end) {
        unsigned char ch = *s++;
        if (ascii_isspace(ch)) {
          if (brk_ < SPACE) brk_ = SPACE;
        } else if (ch == 0xc2 && *s == 0xa0) {
          if (brk_ < NBSP) brk_ = NBSP;
          s++;
        } else {
          if (brk_ != NONE) {
            // Output break.
            switch (brk_) {
              case NONE:
                break;

              case NBSP:
                if (html_output_) {
                  text_.append("&nbsp;");
                } else if (!text_.empty()) {
                  text_.push_back(' ');
                }
                break;

              case SPACE:
                if (!text_.empty()) text_.push_back(' ');
                break;

              case NEWLINE:
                if (html_output_) {
                  text_.append("\n<br>");
                } else {
                  text_.push_back('\n');
                }
                break;

              case PARAGRAPH:
                if (html_output_) {
                  text_.append("\n<p>");
                } else {
                  text_.append("\n\n");
                }
                break;
            }
            brk_ = NONE;
          }

          // Output character.
          text_.push_back(ch);
        }
      }
    }
  }

  return true;
}

void WebPageTextExtractor::DebugText(const char *str) {
  // Skip whitespace.
  const char *s = str;
  const char *end = s + strlen(str);
  while (s < end) {
    if (ascii_isspace(*s)) {
      if (brk_ < SPACE) brk_ = SPACE;
    } else if (s[0] == '\xc2' && s[1] == '\xa0') {
      if (brk_ < NBSP) brk_ = NBSP;
    } else {
      break;
    }
    s++;
  }
  if (s == end) return;

  // Output text break.
  switch (brk_) {
    case NONE:
      break;

    case NBSP:
      if (html_output_) {
        text_.append("&nbsp;");
      } else if (!text_.empty()) {
        text_.push_back(' ');
      }
      break;

    case SPACE:
      if (!text_.empty()) text_.push_back(' ');
      break;

    case NEWLINE:
      if (html_output_) {
        text_.append("\n<br>");
      } else {
        text_.push_back('\n');
      }
      break;

    case PARAGRAPH:
      if (html_output_) {
        text_.append("\n<p>");
      } else {
        text_.append("\n\n");
      }
      break;
  }
  brk_ = NONE;

  // Get analysis statistics.
  uint64 signature = nesting_.back().signature;
  int score = analysis_->Score(signature);
  int repeats = analysis_->PhraseCount(str, signature);

  // Get tag signature.
  string id = StrCat("score: ", score, " repeats: ", repeats, "\n");
  StrAppend(&id, "signature: ", signature, "\n");
  for (const TagInfo &ti : nesting_) {
    bool sticky = analysis_->Sticky(ti.signature);
    id.append(ti.keep ? (sticky ? "* " : "+ ") : "- ");
    id.append(ti.id);
    id.push_back('\n');
  }

  // Start span annotation.
  if (html_output_) {
    if (nesting_.back().keep) {
      text_.append("<span style='color:black;' title='");
    } else {
      text_.append("<span style='color:grey;' title='");
    }
    text_.append(id);
    text_.append("' onclick='cc(this, event)'>");
  } else {
    if (nesting_.back().keep) {
      text_.append("[");
    } else {
      text_.append("{");
    }
    text_.append(id);
    text_.append("|");
  }

  // Output text.
  text_.append(s, end - s);

  // End span annotation.
  if (nesting_.back().keep) {
    text_.append(html_output_ ? "</span>" : "]");
  } else {
    text_.append(html_output_ ? "</span>" : "}");
  }
}

}  // namespace nlp
}  // namespace sling


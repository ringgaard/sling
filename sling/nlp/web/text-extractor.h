#ifndef SLING_NLP_WEB_TEXT_EXTRACTOR_H_
#define SLING_NLP_WEB_TEXT_EXTRACTOR_H_

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/stream/input.h"
#include "sling/string/text.h"
#include "sling/util/fingerprint.h"
#include "sling/web/html-parser.h"

namespace sling {
namespace nlp {

// Analysis results for web site.
class WebsiteAnalysis {
 public:
  // Preserve tag.
  void Preserve(uint64 signature);
  void Preserve(const char *tag, const char *cls);

  // Block tag.
  void Block(uint64 signature);
  void Block(const char *tag, const char *cls);

  // Add tag score.
  void ScoreTag(uint64 signature, int score);

  // Add text phrase with tag signature.
  void AddPhrase(const char *phrase, uint64 signature);

  // Finalize analysis after all pages have been analyzed.
  void Finalize();

  // Get tag fingerprints for tags that should be kept.
  void GetFingerprints(std::vector<uint64> *fingerprints) const;

  // Check if tag contains text contents.
  bool Keep(uint64 signature) const {
    auto f = scores_.find(signature);
    if (f == scores_.end()) return false;
    return f->second > 0;
  }

  // Check if tag has been manually blocked.
  bool Blocked(uint64 fp) const {
    return blocked_.count(fp) > 0;
  }

  // Return score for tag signature.
  int Score(uint64 signature) const {
    auto f = scores_.find(signature);
    if (f == scores_.end()) return 0;
    return f->second;
  }

  // Return phrase count.
  int PhraseCount(const char *phrase, uint64 signature) const;

  // Check if tag is sticky.
  bool Sticky(uint64 signature) const {
    return sticky_.count(signature) > 0;
  }

 private:
  struct PhraseInfo {
    int count = 0;       // phrase count
    uint64 signature;    // tag signature
  };

  // Number of pages analyzed.
  int num_pages_ = 0;

  // Tag score map. The key is the nested tag signature and the value is the
  // score. Positive score means keep tag, negative score means discard tag.
  std::unordered_map<uint64, int> scores_;

  // Tag signatures for tags where some of the children must be kept.
  std::unordered_set<uint64> sticky_;

  // Tag signatures for manually blocked tags.
  std::unordered_set<uint64> blocked_;

  // Phrase map for detecting repeated phrases. The key is the fingerprint of
  // the context tag signature and the phrase text.
  std::unordered_map<uint64, PhraseInfo> phrases_;
};

// Analyze web page and gather statistics on web text contents.
class WebPageAnalyzer : public HTMLParser {
 public:
  WebPageAnalyzer(WebsiteAnalysis *analysis) : analysis_(analysis) {}

  // HTML parse handlers.
  bool StartElement(const XMLElement &e) override;
  bool EndElement(const char *name) override;
  bool Text(const char *str) override;

 private:
  // Information for tag.
  struct TagInfo {
    TagInfo(int64 signature, int text_length)
        : signature(signature),
          text_length(text_length) {}

    // Nested tag signature.
    uint64 signature;

    // Size of extracted text including text extracted from parent tags.
    int text_length;

    // Whether any child tags have contents.
    bool keep_children = false;

    // Whether we should keep text contents from tag.
    bool keep = false;

    // Whether this tag has been manually blocked.
    bool blocked = false;
  };

  // Artificial root tag.
  TagInfo root_{0, 0};

  // Flags to keep track of non-content sections in HTML file.
  bool in_body_ = false;
  bool in_style_ = false;
  bool in_script_ = false;

  // Paragraph level.
  int paragraph_level_ = 0;

  // Tag stack for nested tags.
  std::vector<TagInfo> nesting_;

  // Analysis results for web site.
  WebsiteAnalysis *analysis_;
};

// Extract text from web page.
class WebPageTextExtractor : public HTMLParser {
 public:
  typedef std::vector<std::pair<string, string>> Meta;

  WebPageTextExtractor(WebsiteAnalysis *analysis) : analysis_(analysis) {}

  // HTML parse handlers.
  bool StartElement(const XMLElement &e) override;
  bool EndElement(const char *name) override;
  bool Text(const char *str) override;

  // Web page meta data.
  const Meta &meta() const { return meta_; }

  // Linked data.
  const std::vector<string> &jsonld() const { return jsonld_; }

  // Extracted text.
  const string &text() const { return text_; }

  // Output HTML in extracted text.
  bool html_output() const { return html_output_; }
  void set_html_output(bool html_output) { html_output_ = html_output; }

  // Enable debug mode.
  bool debug() const { return debug_; }
  void set_debug(bool debug) { debug_ = debug; }

 private:
  // Output debug information for text node.
  void DebugText(const char *str);

  // Text break types.
  enum Break {NONE, NBSP, SPACE, NEWLINE, PARAGRAPH};

  // Tag information for tag stack.
  struct TagInfo {
    TagInfo(uint64 signature, bool keep) : signature(signature), keep(keep) {}
    uint64 signature;
    bool keep;
    string id;
  };

  // Web page analysis for web site.
  WebsiteAnalysis *analysis_;

  // Artificial root tag.
  TagInfo root_{0, true};

  // Flags to keep track of non-content sections in HTML file.
  bool in_body_ = false;
  bool in_style_ = false;
  bool in_script_ = false;
  bool in_title_ = false;
  bool in_jsonld_ = false;

  // Tag stack for nested tags.
  std::vector<TagInfo> nesting_;

  // Web page meta information.
  Meta meta_;
  string title_;
  std::vector<string> jsonld_;

  // Extracted text.
  string text_;

  // Current break level.
  Break brk_ = NONE;

  // Output HTML tags in extracted text.
  bool html_output_ = false;

  // In debug mode, all text is extracted but annotated with debug information.
  bool debug_ = false;
};

// Web page meta data.
struct WebPageMetadata {
  // Extract metadata from web page.
  WebPageMetadata(const WebPageTextExtractor &page);

  // Get meta data field.
  Text GetMeta(Text field);

  // Truncate text at character.
  static Text Truncate(Text text, char delim);
  static Text Truncate(Text text, Text delim);

  // Metadata fields from text extractor.
  const WebPageTextExtractor::Meta &meta;

  // Consolidated web page metadata.
  Text type;       // web page type
  Text title;      // page title
  Text summary;    // article summary
  Text url;        // (canonical) page url
  Text image;      // image url
  Text site;       // web site identifier
  Text domain;     // web site domain
  Text language;   // page language
  Text author;     // page author
  Text creator;    // page creator
  Text publisher;  // web page publisher
  Text published;  // publication date
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_WEB_TEXT_EXTRACTOR_H_


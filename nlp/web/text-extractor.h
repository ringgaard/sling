#ifndef NLP_WEB_TEXT_EXTRACTOR_H_
#define NLP_WEB_TEXT_EXTRACTOR_H_

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "base/logging.h"
#include "base/types.h"
#include "stream/input.h"
#include "util/fingerprint.h"
#include "web/html-parser.h"

namespace sling {
namespace nlp {

// Analysis results for web site.
class WebsiteAnalysis {
 public:
  // Add web page to analysis. Return false if this is duplicate page.
  bool AddPage(const string &url);

  // Add tag score.
  void AddTag(uint64 signature, int score);

  // Preserve tag.
  void PreserveTag(uint64 signature);

  // Block tag.
  void Block(const char *tag, const char *cls);

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
  // score. Positive scores means keep tag, negative scores means discard tag.
  std::unordered_map<uint64, int> scores_;

  // Tag signatures for tags where some of the children must be kept.
  std::unordered_set<uint64> sticky_;

  // Tag signatures for manually blocked tags.
  std::unordered_set<uint64> blocked_;

  // Phrase map for detecting repeated phrases. The key is the fingerprint of
  // the context tag signature and the phrase text.
  std::unordered_map<uint64, PhraseInfo> phrases_;

  // URL map with URL fingerprints of all analyzed pages.
  std::unordered_set<uint64> urls_;
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

  // URL for page extracted from meta data.
  string url_;

  // Analysis results for web site.
  WebsiteAnalysis *analysis_;
};

// Extract text from web page.
class WebPageTextExtractor : public HTMLParser {
 public:
  WebPageTextExtractor(WebsiteAnalysis *analysis) : analysis_(analysis) {}

  // HTML parse handlers.
  bool StartElement(const XMLElement &e) override;
  bool EndElement(const char *name) override;
  bool Text(const char *str) override;

  // Web page meta data.
  const string url() const { return url_; }
  const string site() const { return site_; }
  const string type() const { return type_; }
  const string date() const { return date_; }
  const string title() const {
    return title_.empty() ? page_title_ : title_;
  }

  // Output HTML in extracted text.
  bool html_output() const { return html_output_; }
  void set_html_output(bool html_output) { html_output_ = html_output; }

  // Enable debug mode.
  bool debug() const { return debug_; }
  void set_debug(bool debug) { debug_ = debug; }

  // Extracted text.
  const string text() const { return text_; }

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

  // Tag stack for nested tags.
  std::vector<TagInfo> nesting_;

  // Web page meta information.
  string url_;
  string site_;
  string type_;
  string date_;
  string title_;
  string page_title_;

  // Extracted text.
  string text_;

  // Current break level.
  Break brk_ = NONE;

  // Output HTML tags in extracted text.
  bool html_output_ = false;

  // In debug mode, all text is extracted but annotated with debug information.
  bool debug_ = false;
};

}  // namespace nlp
}  // namespace sling

#endif  // NLP_WEB_TEXT_EXTRACTOR_H_


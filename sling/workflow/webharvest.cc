#include <time.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/stream/file-input.h"
#include "sling/stream/memory.h"
#include "sling/string/ctype.h"
#include "sling/string/printf.h"
#include "sling/string/split.h"
#include "sling/string/strip.h"
#include "sling/task/accumulator.h"
#include "sling/task/container.h"
#include "sling/task/task.h"
#include "sling/util/unicode.h"
#include "sling/web/html-parser.h"
#include "sling/web/rfc822-headers.h"
#include "sling/web/url.h"
#include "sling/workflow/common.h"

DEFINE_int32(segments, -1, "Maximum number of WARC segments");
DEFINE_int32(bufsize, -1, "WARC file buffer size");

using namespace sling;
using namespace sling::task;

// HTML article date properties.
std::unordered_map<string, int> date_properties = {
  {"aja:published_date", 5},
  {"article_date_original", 5},
  {"article:published_time", 5},
  {"bt:pubdate", 5},
  {"datepublished", 5},
  {"gwa_pubdate", 5},
  {"item-publish-date", 5},
  {"og:article:published_time", 5},
  {"og:article:publish_time", 5},
  {"og:pubdate", 5},
  {"parsely-pub-date", 5},
  {"prism.publicationdate", 5},
  {"pub_date", 5},
  {"pubdate", 5},
  {"publication_date", 5},
  {"publicationdate", 5},
  {"publish_date", 5},
  {"publish-date", 5},
  {"published-date", 5},
  {"published_time", 5},
  {"rnews:datepublished", 5},
  {"shareaholic:article_published_time", 5},
  {"t_omni_pubdate", 5},
  {"vr:published_time", 5},

  {"dc.date.created", 4},
  {"eprints.date", 4},
  {"eprints.datestamp", 4},
  {"firstcreateddatetime", 4},

  {"date", 3},
  {"dc.date", 3},
  {"dc:date", 3},
  {"dc.date.datesubmitted", 3},
  {"dc.date.issued", 3},
  {"dcterms.date", 3},
  {"sailthru.date", 3},

  {"article:modified_time", 2},
  {"article.updated", 2},
  {"datemodified", 2},
  {"dc.date.modified", 2},
  {"lastmodifieddate", 2},
  {"lastmodifieddatetime", 2},
  {"og:updated_time", 2},
  {"og:article:modified_time", 2},
  {"shareaholic:article_modified_time", 2},
  {"bt:moddate", 2},
  {"revision_date", 2},
  {"aja:modified_date", 2},
  {"last-modified-date", 2},

  {"article_date_updated", 1},
  {"citation_date", 1},
  {"cxenseparse:recs:publishtime", 1},
  {"date_published", 1},
  {"dc.date.available", 1},
  {"dc.date.updated", 1},
  {"dcterms.dateaccepted", 1},
  {"eprints.date", 1},
  {"last-updated", 1},
  {"publishdate", 1},
  {"og:start_time", 1},
  {"og:og:regdate", 1},
  {"document-date", 1},
  {"citation_publication_date", 1},
  {"creation_date", 1},
  {"citation_online_date", 1},
  {"enterdate", 1},
  {"updated", 1},
  {"datestamp", 1},
  {"timestamp", 1},
  {"lingo:date", 1},
  {"isodate", 1},
  {"pdate", 1},
  {"ptime", 1},
  {"live_date", 1},
  {"displaydate", 1},

  {"msvalidate.01", -1},
  {"eprints.date_type", -1},
  {"server_date", -1},
  {"tp:preferredruntimes", -1},
};

// Compare two tags.
static bool TagEqual(const char *t1, const char *t2) {
  return strcasecmp(t1, t2) == 0;
}

// Normalize and trim string.
static void Normalize(Text str, string *result) {
  StripWhiteSpace(&str);
  UTF8::Normalize(str.data(), str.size(), result);
}

// Get two-letter normalized language code.
static string LanguageCode(Text str) {
  string lang;
  Normalize(str, &lang);
  if (lang.size() > 2) lang.resize(2);
  return lang;
}

// Check if charset is utf-8.
static bool IsUTF8(Text charset) {
  return charset == "utf8";
}

// Check if charset is latin-1 compatible.
static bool IsLatin1(Text charset) {
  return charset == "iso88591" || charset == "iso_88591" ||
         charset == "windows1252" || charset == "latin1" ||
         charset == "usascii" || charset == "ascii";
}

// Parse content type and return true if it is HTML.
static bool IsHTMLContent(Text content_type, string *charset) {
  // Trim and normalize content type.
  string ct;
  Normalize(content_type, &ct);

  // Split content type into fields separated by semicolon.
  std::vector<Text> fields = Split(ct, ";",  SkipWhitespace());
  bool html = false;
  for (Text field : fields) {
    StripWhiteSpace(&field);
    if (field == "text/html" || field == "application/xhtml+xml") {
      html = true;
    } else if (charset->empty() && field.starts_with("charset=")) {
      *charset = field.substr(8).str();
    }
  }
  TrimString(charset, "\"'");
  return html;
}

// Parse date.
bool ParseDate(const char *str, struct tm *tm) {
  static const char *date_formats[] = {
    "%Y-%m-%dT%H:%M:%S%z",        // 2013-05-07T10:00:00+00:00
    "%Y-%m-%dT%H:%M:%S%Z",        // 2015-06-15T10:54:00TZD
    "%Y-%m-%dT%H:%M:%S.0000000",  // 2014-04-14T21:54:26.0000000
    "%Y-%m-%dT%H:%M:%S",          // 2013-01-23T04:45:22
    "%Y-%m-%dT%H:%M",             // 2014-07-25T12:39
    "%A, %B %d, %Y, %r",          // Friday, June 14, 2013,  9:57:10 PM
    "%A, %B %d, %Y, %I:%M %p",    // Wednesday, January 23, 2013,  6:54 PM
    "%a, %d %b %Y %H:%M:%S%Z",    // Wed, 11 Jun 2014 11:42:45 EDT
    "%a, %d %b %Y %H:%M:%S%z",    // Fri, 05 May 2011 04:26:18 -07:00
    "%a %b %d, %Y %I:%M%p",       // Tue Jan 15, 2013 10:07 AM
    "%a %b %d %H:%M:%S%Z %Y",     // Sat Jul 25 19:42:13 IDT 2009
    "%a %b %d, %Y %r",            // Fri Feb  8, 2013 11:25 PM
    "%A, %b. %d, %Y",             // Monday, Nov. 07, 1984
    "%A, %B %d, %Y, %I:%M%p",     // Friday, June 14, 2013,  9:57 PM
    "%B %d, %Y %I:%M %p",         // March 25, 2016 4:49 pm
    "%B %d, %Y %H:%M:%S %Z",      // September 06, 2011 19:02:54 MST
    "%B %d, %Y, %I:%M %p",        // July 1, 2016, 3:27 pm
    "%Y-%m-%dT%H:%M:%S.000%z",    // 2009-07-31T22:00:00.000Z
    "%Y-%m-%dT%H:%M%z",           // 2016-02-19T10:00Z
    "%Y-%m-%d",                   // 2008-12-11
    "%Y-%m-%d %H:%M:%S",          // 2008-12-02 2:27:10
    "%Y-%m-%d %r",                // 2016-06-30 03:12:38 PM
    "%Y-%m-%d %H:%M:%S%z",        // 2013-08-05 10:53:18 -0400
    "%Y-%m-%dT%H:%M%z",           // 2014-01-23T00:00+07:00
    "%Y/%m/%d %H:%M:%S",          // 2012/07/09 10:11:55
    "%Y/%m/%d",                   // 2014/05/23
    "%Y%m%d%H%M%S",               // 20121128201937
    "%Y%m%d%H%M",                 // 201309120005
    "%Y%m%d",                     // 20121128
    "%Y%m%dT%H:%M:%S%z",          // 20140514T16:46:00Z
    "%B %d, %Y | %I:%M %p",       // March 27, 2012 | 2:30 pm
    "%B %d, %Y",                  // October 23, 2006
    "%m/%d/%Y %I:%M%p",           // 06/13/2007 06:00AM
    "%m/%d/%Y %I:%M %p",          // 4/8/2011 11:32 PM
    "%m/%d/%Y %H:%M",             // 07/25/2014 03:03
    "%m/%d/%Y %H:%M:%S %Z",       // 04/05/2013 15:29:04 EDT
    "%H:%M , %d.%m.%y",           // 14:51 , 01.12.05
    "%a, %b %d, %Y",              // Tue, Jan 19, 2016
    "%b %d, %Y",                  // Apr 08, 2011
    "%B %d, %Y",                  // October 23, 2006
    "%a %b %d, %Y %H:%M%p ",      // Tue Jan 15, 2013 10:07 AM
    nullptr,
  };

  while (ascii_isspace(*str)) str++;
  for (const char **fmt = date_formats; *fmt; ++fmt) {
    char *result = strptime(str, *fmt, tm);
    if (result != nullptr) {
      while (ascii_isspace(*result)) result++;
      if (*result == 0) return true;
    }
  }

  return false;
}

// Get meta data information from HTML header.
class WebPageMetaInfoParser : public HTMLParser {
 public:
  std::vector<std::pair<string, string>> dates;

  bool StartElement(const XMLElement &e) override {
    if (TagEqual(e.name, "html")) {
      // Get language from html tag.
      const char *lang = e.Get("lang");
      if (!lang) lang = e.Get("xml:lang");
      if (lang) language_ = LanguageCode(lang);
    } else if (TagEqual(e.name, "meta")) {
      // Parse meta tag.
      const char *property = e.Get("property");
      if (!property) property = e.Get("itemprop");
      if (!property) property = e.Get("name");
      const char *content = e.Get("content");
      if (property && *property && content && *content) {
        string name;
        UTF8::Lowercase(property, strlen(property), &name);

        // Get language.
        if (name == "language" ||
            name == "dc.language" ||
            name == "og:locale") {
          language_ = LanguageCode(content);
        }

        // Get document type.
        if (name == "og:type") {
          type_ = content;
          StripWhiteSpace(&type_);
        }

        // Get site.
        if (name == "og:site_name") {
          site_ = content;
          StripWhiteSpace(&site_);
        }

        // Get canonical url.
        if (name == "og:url" || name == "url") {
          if (strstr(content, "://") != nullptr) {
            url_ = content;
            StripWhiteSpace(&url_);
          }
        }

        // Get title.
        if (name == "og:title") {
          title_ = content;
          StripWhiteSpace(&title_);
        }

        // Get publication date.
        auto fd = date_properties.find(name);
        if (fd != date_properties.end()) {
          if (fd->second > date_quality_) {
            struct tm tm;
            if (ParseDate(content, &tm)) {
              date_ = StringPrintf("%04d-%02d-%02d",
                                   tm.tm_year + 1900,
                                   tm.tm_mon + 1,
                                   tm.tm_mday);
              date_quality_ = fd->second;
              //LOG(INFO) << "Parsed date " << content << " to " << date_;
            } else {
              //dates.emplace_back(name, StrCat("FMT: ", content));
            }
          }
        } else if (strcasestr(property, "date") || strcasestr(property, "time")) {
          dates.emplace_back(name, content);
        }
      }

      // Try to get charset from meta charset header.
      const char *charset = e.Get("charset");
      if (charset) {
        Normalize(charset, &charset_);
      }

      // Try to get charset or from meta http-equiv header.
      const char *http_equiv = e.Get("http-equiv");
      if (http_equiv && content) {
        if (TagEqual(http_equiv, "Content-Type")) {
          IsHTMLContent(content, &charset_);
        } else if (TagEqual(http_equiv, "Content-Language")) {
          language_ = LanguageCode(content);
        }
      }
    } else if (TagEqual(e.name, "link")) {
      // Get canonical url.
      const char *rel = e.Get("rel");
      const char *href = e.Get("href");
      if (rel && href) {
        if (TagEqual(rel, "canonical") && strstr(href, "://") != nullptr) {
          url_ = href;
          StripWhiteSpace(&url_);
        }
      }
    } else if (TagEqual(e.name, "body")) {
      // Stop parsing when body is reached.
      return false;
    } else if (TagEqual(e.name, "title")) {
      title_.clear();
      in_title_ = true;
    }
    return true;
  }

  bool EndElement(const char *name) override {
    // Stop parsing when end of header is reached.
    if (TagEqual(name, "head")) {
      return false;
    } else if (TagEqual(name, "title")) {
      in_title_ = false;
      StripWhiteSpace(&title_);
    }

    return true;
  }

  bool Text(const char *str) override {
    if (in_title_) title_.append(str);
    return true;
  }

  // Web page meta data.
  const string charset() const { return charset_; }
  const string language() const { return language_; }
  const string url() const { return url_; }
  const string title() const { return title_; }
  const string date() const { return date_; }
  const string site() const { return site_; }
  const string type() const { return type_; }

 private:
  bool in_title_ = false;
  string charset_;
  string language_;
  string url_;
  string title_;
  string site_;
  string type_;
  string date_;
  int date_quality_ = 0;
};

class WebMetaInfo : public Processor {
 public:
  enum Encoding {UNKNOWN, UTF8, LATIN1, OTHER};

  void Start(Task *task) override {
    // Read domain list.
    Binding *domain_list = task->GetInput("domains");
    CHECK(domain_list != nullptr);
    FileInput domains(domain_list->resource()->name());
    string line;
    while (domains.ReadLine(&line)) {
      StripWhiteSpace(&line);
      if (line.empty() || line[0] == '#') continue;
      domains_.insert(line);
    }

    // Initialize accumulator.
    accumulator_.Init(task->GetSink("output"));

    // Statistics.
    num_html_ = task->GetCounter("html");
    num_non_html_ = task->GetCounter("non_html");

    num_utf8_ = task->GetCounter("utf8");
    num_latin1_ = task->GetCounter("latin1");
    num_unknown_charset_ = task->GetCounter("unknown_charset");
    num_other_charset_ = task->GetCounter("other_charset");

    num_english_ = task->GetCounter("english");
    num_non_english_ = task->GetCounter("non_english");
    num_unknown_lang_ = task->GetCounter("unknown_language");

    num_articles_ = task->GetCounter("num_articles");
    num_article_dates_ = task->GetCounter("num_article_dates");
  }

  void Receive(Channel *channel, Message *message) override {
    // Parse WARC headers.
    RFC822Headers warc;
    ParseWARCHeaders(message->key(), &warc);

    // Parse HTTP headers.
    ArrayInputStream stream(message->value().data(), message->value().size());
    Input input(&stream);
    RFC822Headers http;
    http.Parse(&input);

    // Get content type.
    Text content_type = http.Get("Content-Type");
    string charset;
    bool html = IsHTMLContent(content_type, &charset);

    // Determine character encoding.
    Encoding encoding = UNKNOWN;
    if (!charset.empty()) {
      if (IsUTF8(charset)) {
        encoding = UTF8;
      } else if (IsLatin1(charset)) {
        encoding = LATIN1;
      } else {
        encoding = OTHER;
      }
    }

    // Get content language.
    string language = LanguageCode(http.Get("Content-Language"));

    // Get document date.
    string date; // = http.Get("Last-Modified").str();

    // Get URL.
    string url = warc.Get("WARC-Target-URI").str();

    // Parse HTML header.
    WebPageMetaInfoParser meta;
    if (html && encoding != OTHER) {
      meta.Parse(&input);

      if (!meta.language().empty()) {
        language = meta.language();
      }

      if (!meta.charset().empty()) {
        if (IsUTF8(meta.charset())) {
          encoding = UTF8;
        } else if (IsLatin1(meta.charset())) {
          encoding = LATIN1;
        } else {
          encoding = OTHER;
        }
      }

      if (!meta.date().empty()) {
        date = meta.date();
      }

      if (!meta.url().empty()) {
        url = meta.url();
      }
    }

    // Tally up statistics.
    if (!meta.type().empty() && !meta.site().empty()) {
      string type;
      UTF8::Normalize(meta.type(), &type);
      if (type == "article") {
        URL canonical(url);
        if ((language == "en" || language.empty()) &&
            InDomain(canonical.host())) {
          LOG(INFO) << canonical.host() << " - " << meta.title()
                    << " (" << date << ")";
          num_articles_->Increment();
          if (!date.empty()) {
            num_article_dates_->Increment();
          } else {
            for (auto d : meta.dates) {
              LOG(INFO) << "*** DATE " << canonical.host() << " " << d.first << " = " << d.second;
            }
          }
          if (!date.empty()) accumulator_.Increment(date.substr(0, 4));
        }
      }
    }

    if (html) {
      num_html_->Increment();

      if (language == "en") {
        num_english_->Increment();
      } else if (language.empty()) {
        num_unknown_lang_->Increment();
      } else {
        num_non_english_->Increment();
      }

      switch (encoding) {
        case UNKNOWN: num_unknown_charset_->Increment(); break;
        case UTF8: num_utf8_->Increment(); break;
        case LATIN1: num_latin1_->Increment(); break;
        case OTHER: num_other_charset_->Increment(); break;
      }
    } else {
      num_non_html_->Increment();
    }

    delete message;
  }

  void Done(Task *task) override {
    accumulator_.Flush();
  }

 private:
  // Parse WARC headers.
  void ParseWARCHeaders(Slice data, RFC822Headers *headers) {
    ArrayInputStream stream(data.data(), data.size());
    Input input(&stream);
    headers->Parse(&input);
  }

  // Check if host is in domain.
  bool InDomain(const string &host) {
    if (domains_.count(host) > 0) return true;
    int dot = host.find('.');
    if (dot != -1 && domains_.count(host.substr(dot + 1)) > 0) return true;
    return false;
  }

  // Web domains to harvest.
  std::unordered_set<string> domains_;

  // Accumulator for content types.
  Accumulator accumulator_;

  // Statistics.
  Counter *num_html_ = nullptr;
  Counter *num_non_html_ = nullptr;

  Counter *num_utf8_ = nullptr;
  Counter *num_latin1_ = nullptr;
  Counter *num_unknown_charset_ = nullptr;
  Counter *num_other_charset_ = nullptr;

  Counter *num_english_ = nullptr;
  Counter *num_non_english_ = nullptr;
  Counter *num_unknown_lang_ = nullptr;

  Counter *num_articles_ = nullptr;
  Counter *num_article_dates_ = nullptr;
};

REGISTER_TASK_PROCESSOR("web-meta-info", WebMetaInfo);

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up workflow.
  LOG(INFO) << "Set up workflow";
  Container wf;
  ResourceFactory rf(&wf);

  // Web corpus reader.
  WebCorpus web(&wf);
  if (FLAGS_segments > 0) web.SetFileLimit(FLAGS_segments);
  if (FLAGS_bufsize > 0) web.SetBufferSize(FLAGS_bufsize);

  // Web page meta info processor.
  Task *metainfo = wf.CreateTask("web-meta-info", "web-meta-info");
  web.Connect(&wf, metainfo);
  wf.BindInput(metainfo, rf.File("newssites.txt", "text"), "domains");

  // Shuffle and reduce.
  Task *sorter = wf.CreateTask("sorter", "sorter");
  wf.Connect(metainfo, sorter, "count");
  Task *summer = wf.CreateTask("sum-reducer", "summer");
  wf.Connect(sorter, summer, "count");

  // Output stats.
  Writer writer(&wf, "writer", rf.Files("webstat.txt", "textmap/count"));
  writer.Connect(&wf, summer);

  // Run.
  LOG(INFO) << "Run workflow";
  wf.Run();
  while (!wf.Wait(15000)) wf.DumpCounters();

  wf.Wait();

  LOG(INFO) << "Done workflow";
  wf.DumpCounters();

  return 0;
}


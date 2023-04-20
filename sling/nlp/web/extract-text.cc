#include <iostream>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/nlp/web/text-extractor.h"
#include "sling/stream/file-input.h"
#include "sling/string/strip.h"
#include "sling/web/rfc822-headers.h"
#include "sling/web/web-archive.h"

DEFINE_bool(html, false, "Output HTML tags in extracted text");

DEFINE_int32(max, -1, "Maximum extracted articles");

DEFINE_bool(debug, false, "Output debug annotations");

DEFINE_string(filter,
              "/var/data/corpora/news/site-filters.txt",
              "Web site text extraction filters");

using namespace sling;
using namespace sling::nlp;

// HTML header.
static const char *html_header = R"""(
  <html>
  <head>
  <meta charset='utf-8'/>
  <script>
    function cc(elem, event) {
      if (event.ctrlKey) {
        var dialog = document.getElementById('dialog');
        var msg = document.getElementById('message');
        msg.innerHTML = elem.title.replace(/\n/g, "<br>");
        dialog.showModal();
      }
    }
  </script>
  </head>
  <body>
    <dialog id='dialog'>
      <pre id='message'></pre>
      <button onclick="document.getElementById('dialog').close()">
        Close
      </button>
   </dialog>
)""";

// HTML footer.
static const char *html_footer = R"""(
  </body>
  </html>
)""";

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Output HTML header in HTML output mode.
  if (FLAGS_html) std::cout << html_header;

  // Initialize analysis and load content filters.
  WebsiteAnalysis analysis;
  FileInput filters(FLAGS_filter);
  string line;
  while (filters.ReadLine(&line)) {
    StripWhiteSpace(&line);
    if (line.empty() || line[0] == '#') continue;
    int comma = line.find(',');
    CHECK_NE(comma, -1) << "Invalid filter line: " << line;
    string tag = line.substr(0, comma);
    string cls = line.substr(comma + 1);
    analysis.Block(tag.c_str(), cls.c_str());
  }

  // Analyze web pages.
  int num_articles = 0;
  for (int i = 1; i < argc; ++i) {
    WARCFile warc(argv[i]);
    while (warc.Next()) {
      Input input(warc.content());
      RFC822Headers headers;
      headers.Parse(&input);
      WebPageAnalyzer analyzer(&analysis);
      analyzer.Parse(&input);
      num_articles++;
    }
  }
  analysis.Finalize();
  std::vector<uint64> fingerprints;
  analysis.GetFingerprints(&fingerprints);
  LOG(INFO) << num_articles << " articles, "
            << fingerprints.size() << " fingerprints";

  // Extract text from web pages.
  int num_pages = 0;
  for (int i = 1; i < argc; ++i) {
    const char *filename = argv[i];
    if (FLAGS_max != -1 && num_pages > FLAGS_max) break;
    WARCFile warc(filename);
    while (warc.Next()) {
      if (FLAGS_max != -1 && ++num_pages > FLAGS_max) break;

      Input input(warc.content());
      RFC822Headers headers;
      headers.Parse(&input);

      WebPageTextExtractor extractor(&analysis);
      if (FLAGS_html) extractor.set_html_output(true);
      if (FLAGS_debug) extractor.set_debug(true);
      extractor.Parse(&input);

      if (FLAGS_html) {
        std::cout << "<hr>\n"
                  << "<b>File:</b> <a href='file://" << filename << "'>"
                  << filename << "</a><br>\n"
                  << "<div style='width: 700px;'>"
                  << extractor.text()
                  << "</div>\n";
      } else {
        std::cout << "-------------------------------------------------------\n"
                  << "File: " << filename << "\n"
                  << "\n" << extractor.text() << "\n\n";
      }
    }
  }

  // Output HTML footer.
  if (FLAGS_html) std::cout << html_footer;

  return 0;
}


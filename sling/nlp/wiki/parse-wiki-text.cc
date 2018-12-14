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

#include <iostream>
#include <string>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/file/file.h"
#include "sling/frame/serialization.h"
#include "sling/frame/store.h"
#include "sling/nlp/wiki/wiki.h"
#include "sling/nlp/wiki/wiki-annotator.h"
#include "sling/nlp/wiki/wiki-extractor.h"
#include "sling/nlp/wiki/wiki-parser.h"
#include "sling/nlp/wiki/wikipedia-map.h"

DEFINE_string(input, "test.txt", "input file with wiki text");
DEFINE_string(lang, "", "language for wiki text");

using namespace sling;
using namespace sling::nlp;

class Resolver : public WikiLinkResolver {
 public:
  void Init() {
    string dir = "local/data/e/wiki/" + FLAGS_lang;
    wikimap_.LoadRedirects(dir + "/redirects.sling");
    wikimap_.LoadMapping(dir + "/mapping.sling");
  }

  Text ResolveLink(Text link) override {
    if (link.find('#') != -1) return Text();
    return wikimap_.LookupLink(FLAGS_lang, link, WikipediaMap::ARTICLE);
  }

  Text ResolveTemplate(Text link) override {
    return wikimap_.LookupLink(FLAGS_lang, "Template", link,
                               WikipediaMap::TEMPLATE);
  }

  Text ResolveCategory(Text link) override {
    return wikimap_.LookupLink(FLAGS_lang, "Category", link,
                               WikipediaMap::CATEGORY);
  }

  Store *store() { return wikimap_.store(); }

 private:
  WikipediaMap wikimap_;
};

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  Resolver resolver;
  Store *store = resolver.store();
  if (!FLAGS_lang.empty()) {
    resolver.Init();
    LoadStore("data/wiki/templates-" + FLAGS_lang + ".sling", store);
  }

  string wikitext;
  CHECK(File::ReadContents(FLAGS_input, &wikitext));

  WikiParser parser(wikitext.c_str());
  parser.Parse();

  WikiExtractor extractor(parser);
  WikiAnnotator annotator(store, &resolver);
  Frame template_config(store, "/wp/templates/" + FLAGS_lang);
  WikiTemplateRepository templates;
  if (template_config.valid()) {
    templates.Init(&resolver, template_config);
    annotator.set_templates(&templates);
  }

  extractor.Extract(&annotator);

  WikiPlainTextSink intro;
  extractor.ExtractIntro(&intro);

  std::cout << "<html>\n";
  std::cout << "<head>\n";
  std::cout << "<meta charset='utf-8'/>\n";
  std::cout << "</head>\n";
  std::cout << "<body>\n";
  std::cout <<  annotator.text() << "\n";
  std::cout << "<h1>AST</h1>\n<pre>\n";
  if (!intro.text().empty()) {
    std::cout << "Intro: " << intro.text() << "<br><br>";
  }
  parser.PrintAST(0, 0);
  std::cout << "</pre>\n";
  std::cout << "</body></html>\n";

  return 0;
}


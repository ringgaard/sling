#include <iostream>
#include <string>
#include <vector>

#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/base/flags.h"
#include "sling/file/file.h"
#include "sling/frame/object.h"
#include "sling/frame/serialization.h"
#include "sling/nlp/document/document.h"
#include "sling/nlp/document/document-tokenizer.h"
#include "sling/nlp/document/lex.h"
#include "sling/nlp/kb/phrase-table.h"
#include "sling/nlp/ner/chart.h"
#include "sling/nlp/ner/measures.h"

DEFINE_string(text, "", "Text to parse");
DEFINE_string(input, "", "File with text to parse");

using namespace sling;
using namespace sling::nlp;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  string text;
  if (!FLAGS_text.empty()) {
    text = FLAGS_text;
  } else if (!FLAGS_input.empty()) {
    CHECK(File::ReadContents(FLAGS_input, &text));
  }
  
  StopWords stopwords;
  stopwords.Add(".");
  stopwords.Add("``");
  stopwords.Add("the");
  stopwords.Add("a");
  stopwords.Add("an");
  stopwords.Add("in");
  stopwords.Add("of");
  stopwords.Add("is");
  stopwords.Add("was");
  stopwords.Add("by");
  stopwords.Add("and");
  stopwords.Add("to");
  stopwords.Add("at");
  stopwords.Add("'s");

  stopwords.Add("le");
  stopwords.Add("la");
  stopwords.Add("les");
  stopwords.Add("l'");
  //stopwords.Add("do");

  Store store;
  LoadStore("local/data/e/wiki/kb.sling", &store);

  PhraseTable aliases;
  aliases.Load(&store, "local/data/e/wiki/nl/phrase-table.repo");

  SpanTaxonomy taxonomy;
  NumberAnnotator number;
  taxonomy.Init(&store);
  number.Init(&store);
  
  Document document(&store);
  DocumentTokenizer tokenizer;
  DocumentLexer lexer(&tokenizer);
  CHECK(lexer.Lex(&document, text));

  for (SentenceIterator s(&document); s.more(); s.next()) {
    SpanChart chart(&document, s.begin(), s.end(), 10);
    chart.Populate(aliases, stopwords);
    number.Annotate(&chart);
    taxonomy.Annotate(aliases, &chart);
    chart.Solve();
    chart.Extract();
  }
  document.Update();

  std::cout << ToLex(document) << "\n\n";
  //std::cout << ToText(document.top()) << "\n";

  return 0;
}


#include <iostream>
#include <string>
#include <vector>

#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/base/flags.h"
#include "sling/file/file.h"
#include "sling/file/recordio.h"
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
DEFINE_string(item, "", "QID of item to parse");
DEFINE_string(lang, "en", "Language");

using namespace sling;
using namespace sling::nlp;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  StopWords stopwords;
  stopwords.Add(".");
  stopwords.Add(",");
  stopwords.Add("-");
  stopwords.Add(":");
  stopwords.Add(";");
  stopwords.Add("(");
  stopwords.Add(")");
  stopwords.Add("``");
  stopwords.Add("''");
  stopwords.Add("--");
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
  stopwords.Add("as");

  stopwords.Add("le");
  stopwords.Add("la");
  stopwords.Add("les");
  stopwords.Add("l'");
  //stopwords.Add("do");

  Store commons;
  commons.LockGC();
  LoadStore("local/data/e/wiki/kb.sling", &commons);

  PhraseTable aliases;
  aliases.Load(&commons,
               "local/data/e/wiki/" + FLAGS_lang + "/phrase-table.repo");

  RecordFileOptions options;
  RecordDatabase db("local/data/e/wiki/" + FLAGS_lang + "/documents@10.rec",
                    options);

  SpanImporter importer;
  SpanTaxonomy taxonomy;
  NumberAnnotator numbers;
  NumberScaleAnnotator scales;
  MeasureAnnotator measures;
  DateAnnotator dates;

  importer.Init(&commons);
  taxonomy.Init(&commons);
  numbers.Init(&commons);
  scales.Init(&commons);
  measures.Init(&commons);
  dates.Init(&commons);

  commons.Freeze();

  Store store(&commons);
  Frame frame(&store, Handle::nil());
  if (!FLAGS_item.empty()) {
    Record record;
    CHECK(db.Lookup(FLAGS_item, &record));
    frame = Decode(&store, record.value).AsFrame();
  }
  Document document(frame);
  if (frame.IsNil()) {
    string text;
    if (!FLAGS_text.empty()) {
      text = FLAGS_text;
    } else if (!FLAGS_input.empty()) {
      CHECK(File::ReadContents(FLAGS_input, &text));
    }

    DocumentTokenizer tokenizer;
    DocumentLexer lexer(&tokenizer);
    CHECK(lexer.Lex(&document, text));
  }

  document.ClearAnnotations();  // TODO: import annotations into chart

  for (SentenceIterator s(&document); s.more(); s.next()) {
    SpanChart chart(&document, s.begin(), s.end(), 10);
    chart.Populate(aliases, stopwords);

    importer.Annotate(&chart);
    taxonomy.Annotate(aliases, &chart);
    numbers.Annotate(&chart);
    scales.Annotate(aliases, &chart);
    measures.Annotate(aliases, &chart);
    dates.Annotate(aliases, &chart);

    chart.Solve();
    chart.Extract();
  }
  document.Update();

  for (SentenceIterator s(&document); s.more(); s.next()) {
    Document sentence(document, s.begin(), s.end(), true);
    std::cout << "S: " << ToLex(sentence) << "\n";
  }

  //std::cout << ToLex(document) << "\n\n";
  //std::cout << ToText(document.top()) << "\n";

  return 0;
}


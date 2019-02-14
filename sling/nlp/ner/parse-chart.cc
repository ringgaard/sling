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
#include "sling/nlp/ner/idf.h"
#include "sling/nlp/ner/measures.h"

DEFINE_string(text, "", "Text to parse");
DEFINE_string(input, "", "File with text to parse");
DEFINE_string(item, "", "QID of item to parse");
DEFINE_string(lang, "en", "Language");

using namespace sling;
using namespace sling::nlp;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  Store commons;
  commons.LockGC();
  LoadStore("local/data/e/wiki/kb.sling", &commons);

  PhraseTable aliases;
  aliases.Load(&commons,
               "local/data/e/wiki/" + FLAGS_lang + "/phrase-table.repo");

  RecordFileOptions options;
  RecordDatabase db("local/data/e/wiki/" + FLAGS_lang + "/documents@10.rec",
                    options);

  IDFTable dictionary;
  dictionary.Load("local/data/e/wiki/" + FLAGS_lang + "/idf.repo");

  SpanPopulator populator;
  SpanImporter importer;
  SpanTaxonomy taxonomy;
  PersonNameAnnotator persons;
  NumberAnnotator numbers;
  NumberScaleAnnotator scales;
  MeasureAnnotator measures;
  DateAnnotator dates;
  CommonWordPruner pruner;

  importer.Init(&commons);
  taxonomy.Init(&commons);
  persons.Init(&commons);
  numbers.Init(&commons);
  scales.Init(&commons);
  measures.Init(&commons);
  dates.Init(&commons);

  populator.AddStopWord(".");
  populator.AddStopWord(",");
  populator.AddStopWord("-");
  populator.AddStopWord(":");
  populator.AddStopWord(";");
  populator.AddStopWord("(");
  populator.AddStopWord(")");
  populator.AddStopWord("``");
  populator.AddStopWord("''");
  populator.AddStopWord("--");
  populator.AddStopWord("the");
  populator.AddStopWord("a");
  populator.AddStopWord("an");
  populator.AddStopWord("in");
  populator.AddStopWord("of");
  populator.AddStopWord("is");
  populator.AddStopWord("was");
  populator.AddStopWord("by");
  populator.AddStopWord("and");
  populator.AddStopWord("to");
  populator.AddStopWord("at");
  populator.AddStopWord("'s");
  populator.AddStopWord("as");

  //populator.AddStopWord("le");
  //populator.AddStopWord("la");
  //populator.AddStopWord("les");
  //populator.AddStopWord("l'");
  //populator.AddStopWord("do");

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

  //std::cout << "original:\n" << ToLex(document) << "\n\n";

  Document outdoc(document);
  outdoc.ClearAnnotations();

  for (SentenceIterator s(&document); s.more(); s.next()) {
    SpanChart chart(&document, s.begin(), s.end(), 10);

    populator.Annotate(aliases, &chart);
    importer.Annotate(aliases, &chart);
    taxonomy.Annotate(aliases, &chart);
    persons.Annotate(&chart);
    numbers.Annotate(&chart);
    scales.Annotate(aliases, &chart);
    measures.Annotate(aliases, &chart);
    dates.Annotate(aliases, &chart);
    pruner.Annotate(dictionary, &chart);

    chart.Solve();
    chart.Extract(&outdoc);
  }
  outdoc.Update();

  std::cout << ToLex(outdoc) << "\n";

  return 0;
}


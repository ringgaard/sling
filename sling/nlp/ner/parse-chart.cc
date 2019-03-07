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
#include "sling/nlp/ner/annotators.h"
#include "sling/nlp/ner/chart.h"
#include "sling/nlp/ner/idf.h"

DEFINE_string(text, "", "Text to parse");
DEFINE_string(input, "", "File with text to parse");
DEFINE_string(item, "", "QID of item to parse");
DEFINE_string(lang, "en", "Language");
DEFINE_bool(resolve, false, "Resolve annotated entities");

using namespace sling;
using namespace sling::nlp;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Initialize annotator.
  Store commons;
  commons.LockGC();

  SpanAnnotator::Resources resources;
  resources.kb = "local/data/e/wiki/kb.sling";
  resources.aliases = "local/data/e/wiki/" + FLAGS_lang + "/phrase-table.repo";
  resources.dictionary = "local/data/e/wiki/" + FLAGS_lang + "/idf.repo";
  resources.resolve = FLAGS_resolve;

  SpanAnnotator annotator;
  annotator.Init(&commons, resources);

  std::vector<string> stop_words = {
    ".", ",", "-", ":", ";", "(", ")", "``", "''", "'", "--", "/", "&", "?",
    "the", "a", "an", "'s", "is", "was", "and",
    "in", "of", "by", "to", "at", "as",
  };
  annotator.AddStopWords(stop_words);

  commons.Freeze();

  // Open document corpus.
  RecordFileOptions options;
  RecordDatabase db("local/data/e/wiki/" + FLAGS_lang + "/documents@10.rec",
                    options);

  // Initialize document.
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

  // Create unannotated output document.
  Document output(document);
  output.ClearAnnotations();

  // Annotate document.
  annotator.Annotate(document, &output);

  // Output annotated document.
  std::cout << ToLex(output) << "\n";

  return 0;
}


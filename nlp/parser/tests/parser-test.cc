#include "base/init.h"
#include "base/logging.h"
#include "base/types.h"
#include "base/flags.h"
#include "frame/serialization.h"
#include "nlp/document/document.h"
#include "nlp/document/document-tokenizer.h"
#include "nlp/parser/parser.h"

DEFINE_string(parser, "local/parser.flow", "input file with flow model");
DEFINE_string(text, "John hit the ball with a bat.", "Text to parse");

using namespace sling;
using namespace sling::nlp;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Load parser.
  LOG(INFO) << "Load parser from " << FLAGS_parser;
  Store commons;
  Parser parser;
  parser.Load(&commons, FLAGS_parser);
  commons.Freeze();

  // Create document tokenizer.
  DocumentTokenizer tokenizer;

  // Create document
  Store store(&commons);
  Document document(&store);

  // Parse sentence.
  tokenizer.Tokenize(&document, FLAGS_text);
  parser.Parse(&document);
  document.Update();

  LOG(INFO) << ToText(document.top(), 2);

  return 0;
}


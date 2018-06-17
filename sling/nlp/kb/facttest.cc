#include <iostream>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/frame/store.h"
#include "sling/frame/serialization.h"
#include "sling/nlp/kb/facts.h"

DEFINE_string(kb, "local/data/e/wiki/kb.sling", "Knowledge base");
//DEFINE_string(names, "local/data/e/wiki/en/name-table.repo", "Name table");

using namespace sling;
using namespace sling::nlp;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  LOG(INFO) << "Load knowledge base";
  Store commons;
  LoadStore(FLAGS_kb, &commons);

  LOG(INFO) << "Initialize fact catalog";
  FactCatalog catalog;
  catalog.Init(&commons);
  commons.Freeze();

  LOG(INFO) << "Extract facts";
  Store store(&commons);

  Handle helle = store.Lookup("Q57652");
  Facts facts(&catalog, &store);
  facts.Extract(helle);

  std::cout << facts.list().size() << " facts\n";
  for (Handle fact : facts.list()) {
    std::cout << "fact " << ToText(&store, fact) << "\n";
  }

  return 0;
}


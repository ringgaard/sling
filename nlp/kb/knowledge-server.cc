#include "base/flags.h"
#include "base/init.h"
#include "base/logging.h"
#include "http/http-server.h"
#include "nlp/kb/knowledge-service.h"

DEFINE_int32(port, 8080, "HTTP server port");
DEFINE_string(kb, "/var/data/e/wikidata/repository", "Knowledge base");
DEFINE_string(names, "/var/data/e/wikidata/names-en", "Name table");

using namespace sling;
using namespace sling::nlp;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  LOG(INFO) << "Start HTTP server";
  HTTPServerOptions options;
  HTTPServer http(options, FLAGS_port);

  KnowledgeService kb;
  kb.Load(FLAGS_kb, FLAGS_names);
  kb.Register(&http);

  CHECK_OK(http.Start());

  LOG(INFO) << "HTTP server running";
  http.Wait();

  LOG(INFO) << "HTTP server done";
  return 0;
}


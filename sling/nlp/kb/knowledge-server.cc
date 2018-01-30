#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/http/http-server.h"
#include "sling/nlp/kb/knowledge-service.h"

DEFINE_int32(port, 8080, "HTTP server port");
DEFINE_string(kb, "local/data/e/wiki/kb.sling", "Knowledge base");
DEFINE_string(names, "local/data/e/wiki/en/name-table.repo", "Name table");

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

  CHECK(http.Start());

  LOG(INFO) << "HTTP server running";
  http.Wait();

  LOG(INFO) << "HTTP server done";
  return 0;
}


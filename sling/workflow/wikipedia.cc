#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/task/container.h"
#include "sling/workflow/common.h"

DEFINE_string(language, "en", "Wikipedia language");

using namespace sling;
using namespace sling::task;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up workflow.
  LOG(INFO) << "Set up workflow";
  Container wf;
  ResourceFactory rf(&wf);
  string wfdir = Corpora::workflow("wikipedia");

  // Wikipedia dump.
  Resource *wikipedia_dump =
    rf.File(Corpora::wikipedia_dump(FLAGS_language), "xml/wikipage");

  // Wikipedia importer.
  Task *wikipedia_importer =
      wf.CreateTask("wikipedia-importer", "wikipedia");
  wf.BindInput(wikipedia_importer, wikipedia_dump, "input");

  // Wikipedia articles.
  ShardedWriter articles(&wf, "wikipedia-articles",
      rf.Files(wfdir + "/" + FLAGS_language + "/articles@10", "records/frame"));
  articles.Connect(&wf, wikipedia_importer, "articles");

  // Wikipedia redirects.
  FrameStoreBuilder redirects(&wf, "redirects",
      rf.File(wfdir + "/" + FLAGS_language + "/redirects", "store"));
  redirects.Connect(&wf, wikipedia_importer, "redirects");

  // Run.
  LOG(INFO) << "Run workflow";
  wf.Run();
  while (!wf.Wait(15000)) wf.DumpCounters();

  wf.Wait();

  LOG(INFO) << "Done workflow";
  wf.DumpCounters();

  return 0;
}


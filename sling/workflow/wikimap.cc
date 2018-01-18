#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/task/job.h"
#include "sling/workflow/common.h"

DEFINE_string(language, "en", "Wikipedia language");

using namespace sling;
using namespace sling::task;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up workflow.
  LOG(INFO) << "Set up workflow";
  string wfdir = Corpora::workflow("wikidata");
  string infile = wfdir + "/items@10";
  string outfile = wfdir + "/mapping-" + FLAGS_language;
  Job wf;
  ResourceFactory rf(&wf);

  // Wikidata item reader.
  Reader items(&wf, "wiki-items", rf.Files(infile, "records/frame"));

  // Wikipedia mapper.
  Task *wikipedia_mapping =
      wf.CreateTask("wikipedia-mapping", "wikipedia-mapper");
  wikipedia_mapping->AddParameter("language", FLAGS_language);
  items.Connect(&wf, wikipedia_mapping);

  // Frame store writer for Wikipedia-to-Wikidata mapping.
  FrameStoreBuilder writer(&wf, "wikimap", rf.File(outfile, "store"));
  wf.Connect(wikipedia_mapping, writer.builder, "frame");

  // Run.
  LOG(INFO) << "Run workflow";
  wf.Start();
  while (!wf.Wait(15000)) wf.DumpCounters();
  wf.Wait();

  LOG(INFO) << "Done workflow";
  wf.DumpCounters();

  return 0;
}


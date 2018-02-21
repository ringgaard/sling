#include <string>
#include <vector>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/frame/object.h"
#include "sling/task/job.h"
#include "sling/workflow/common.h"

using namespace sling;
using namespace sling::task;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up workflow.
  LOG(INFO) << "Set up workflow";
  string wfdir = Corpora::workflow("wikidata");

  Job wf;
  ResourceFactory rf(&wf);
  Resources items = rf.Files(wfdir + "/items@10", "records/frame");
  Resources properties = rf.Files(wfdir + "/properties", "records/frame");
  Resource *repository = rf.File(wfdir + "/repository", "store");
  Resources schemas = {
    rf.File(Corpora::google3("data/nlp/schemas/languages.sl"), "text/frame"),
    rf.File(Corpora::google3("data/nlp/schemas/calendar.sl"), "text/frame"),
  };

  // Wikidata item reader.
  Reader item_reader(&wf, "wiki-items", items);

  // Wikidata property reader.
  Reader property_reader(&wf, "wiki-properties", properties);

  // Schema reader.
  Reader schema_reader(&wf, "schemas", schemas);

  // Prune information from Wikidata items.
  Task *wikidata_pruner = wf.CreateTask("wikidata-pruner", "wikidata-pruner");
  item_reader.Connect(&wf, wikidata_pruner);

  // Collect property catalog.
  Task *property_collector = wf.CreateTask("wikidata-property-collector",
                                           "property-collector");
  property_reader.Connect(&wf, property_collector);

  // Frame store writer for repository.
  FrameStoreBuilder writer(&wf, "repository", repository);
  wf.Connect(wikidata_pruner, writer.builder, "frame");
  wf.Connect(property_collector, writer.builder, "frame");
  schema_reader.Connect(&wf, writer.builder);

  // Run.
  LOG(INFO) << "Run workflow";
  wf.Start();
  while (!wf.Wait(15000)) wf.DumpCounters();
  wf.Wait();

  LOG(INFO) << "Done workflow";
  wf.DumpCounters();

  return 0;
}


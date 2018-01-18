#include "sling/base/init.h"
#include "sling/base/logging.h"
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

  // Wikidata reader.
  Reader wikidata(&wf, "wikidata", rf.Files(Corpora::wikidata_dump(), "text"));

  // Workers.
  Task *wikidata_workers = wf.CreateTask("workers", "wikidata-workers");
  wikidata_workers->AddParameter("worker_threads", "5");
  wikidata.Connect(&wf, wikidata_workers);

  // Wikidata importer.
  Task *wikidata_importer =
      wf.CreateTask("wikidata-importer", "wikidata-profiles");
  wf.Connect(wikidata_workers, wikidata_importer, "text");

  // Item writers.
  ShardedWriter items(&wf, "items",
      rf.Files(wfdir + "/items", 10, "records/sling"));
  items.Connect(&wf, wikidata_importer, "items");

  // Property writer.
  Writer props(&wf, "properties",
      rf.Files(wfdir + "/properties", "records/sling"));
  props.Connect(&wf, wikidata_importer, "properties");

  // Run.
  LOG(INFO) << "Run workflow";
  wf.Start();
  while (!wf.Wait(15000)) wf.DumpCounters();
  wf.Wait();

  LOG(INFO) << "Done workflow";
  wf.DumpCounters();

  return 0;
}


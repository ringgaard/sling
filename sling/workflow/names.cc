#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/task/container.h"
#include "sling/workflow/common.h"

DEFINE_string(language, "en", "Name language");

using namespace sling;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up workflow.
  LOG(INFO) << "Set up workflow";
  string wfdir = Corpora::workflow("wikidata");
  task::Container wf;
  ResourceFactory rf(&wf);

  // Alias reader.
  Reader aliases(&wf, "aliases",
      rf.Files(wfdir + "/aliases-" + FLAGS_language + "@10", "records/frame"));

  // Name table builder.
  task::Task *name_table_builder =
      wf.CreateTask("name-table-builder", "name-table-builder");
  name_table_builder->AddParameter("language", FLAGS_language);
  aliases.Connect(&wf, name_table_builder);
  wf.BindOutput(name_table_builder,
                rf.File(wfdir + "/names-" + FLAGS_language, "repository/name"),
                "repository");

  // Run.
  LOG(INFO) << "Run workflow";
  wf.Run();
  while (!wf.Wait(15000)) wf.DumpCounters();

  wf.Wait();

  LOG(INFO) << "Done workflow";
  wf.DumpCounters();

  return 0;
}


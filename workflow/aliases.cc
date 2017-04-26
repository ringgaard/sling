#include "base/flags.h"
#include "base/init.h"
#include "base/logging.h"
#include "base/types.h"
#include "task/container.h"
#include "workflow/common.h"

DEFINE_string(language, "en", "Alias language");

using namespace sling;
using namespace sling::task;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up workflow.
  LOG(INFO) << "Set up workflow";
  string wfdir = Corpora::workflow("wikidata");
  string wpdir = Corpora::workflow("wikipedia") + "/" + FLAGS_language;
  Container wf;

  ResourceFactory rf(&wf);
  Resources wikidata_alias_files =
    rf.Files(wfdir + "/items@10", "records/frame");
  Resources wikipedia_alias_files =
    rf.Files(wpdir + "/aliases@10", "records/frame");
  Resource *toxic_aliases =
    rf.File(wfdir + "/toxic-aliases-" + FLAGS_language, "toxic-aliases");
  Resources aliases =
    rf.Files(wfdir + "/aliases-" + FLAGS_language + "@10", "records/frame");

  // Wikidata item reader.
  Reader items(&wf, "wiki-items", wikidata_alias_files);

  // Wikidata alias mapper.
  Task *wikidata_mapper =
      wf.CreateTask("profile-alias-extractor", "wikidata-alias-extractor");
  wikidata_mapper->AddParameter("language", FLAGS_language);
  items.Connect(&wf, wikidata_mapper);

  // Wikipedia aliases.
  Reader wikipedia_aliases(&wf, "wikipedia-aliases", wikipedia_alias_files);

  // Alias merging.
  Shuffle alias_shuffle(&wf, "alias", "id:frame", aliases.size());
  alias_shuffle.Connect(&wf, wikidata_mapper, "id:frame");
  wikipedia_aliases.Connect(&wf, alias_shuffle.sharder);
  Reduce alias_reduce(&wf, "alias", "profile-alias-reducer", aliases);
  alias_reduce.reducer->AddParameter("language", FLAGS_language);
  alias_reduce.Connect(&wf, alias_shuffle, "id:frame");
  wf.BindInput(alias_reduce.reducer, toxic_aliases, "toxic-aliases");

  // Run.
  LOG(INFO) << "Run workflow";
  wf.Run();
  while (!wf.Wait(15000)) wf.DumpCounters();

  wf.Wait();

  LOG(INFO) << "Done workflow";
  wf.DumpCounters();

  return 0;
}


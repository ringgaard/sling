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
  string wfdir = Corpora::workflow("wikipedia") + "/" + FLAGS_language;
  Job wf;
  ResourceFactory rf(&wf);
  Resources articles =
      rf.Files(wfdir + "/articles@10", "records/frame");
  Resource *redirects =
      rf.File(wfdir + "/redirects", "store");
  Resource *wikimap = rf.File(
      Corpora::workflow("wikidata") + "/mapping-" + FLAGS_language, "store");
  Resource *languages =
      rf.File(Corpora::google3("data/nlp/schemas/languages.sl"), "text");
  Resources documents =
      rf.Files(wfdir + "/documents@10", "records/frame");
  Resources aliases =
      rf.Files(wfdir + "/aliases@10", "records/alias");

  // Wikipedia page reader.
  Reader pages(&wf, "articles", articles);

  // Wikipedia profile builder.
  Task *builder =
      wf.CreateTask("wikipedia-profile-builder", "wikipedia-profiles");
  pages.Connect(&wf, builder);
  wf.BindInput(builder, languages, "commons");
  wf.BindInput(builder, wikimap, "wikimap");
  wf.BindInput(builder, redirects, "redirects");

  // Wikipedia document writer.
  ShardedWriter writer(&wf, "wikipedia-documents", documents);
  writer.Connect(&wf, builder);

  // Wikipedia alias map reduction.
  Shuffle alias_shuffle(&wf, "alias", "id:alias", aliases.size());
  alias_shuffle.Connect(&wf, builder, "id:alias", "aliases");
  Reduce alias_reduce(&wf, "alias", "wikipedia-alias-reducer", aliases);
  alias_reduce.reducer->AddParameter("language", FLAGS_language);
  alias_reduce.Connect(&wf, alias_shuffle, "id:alias");

  // Run.
  LOG(INFO) << "Run workflow";
  wf.Start();
  while (!wf.Wait(15000)) wf.DumpCounters();
  wf.Wait();

  LOG(INFO) << "Done workflow";
  wf.DumpCounters();

  return 0;
}


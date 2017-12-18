#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/nlp/wiki/wiki.h"
#include "sling/task/accumulator.h"
#include "sling/task/frames.h"
#include "sling/task/job.h"
#include "sling/task/task.h"
#include "sling/string/numbers.h"
#include "sling/workflow/common.h"

DEFINE_string(language, "en", "Wikipedia language");
DEFINE_int64(threshold, 100, "Toxic alias threshold");

using namespace sling;
using namespace sling::nlp;
using namespace sling::task;

// Aggregate counts for all Wikidata aliases.
class ToxicAliasMapper : public FrameProcessor {
 public:
  void Startup(Task *task) override {
    // Get language for names.
    string lang = task->Get("language", "en");
    language_ = commons_->Lookup("/lang/" + lang);

    // Initialize accumulator.
    accumulator_.Init(output());
  }

  void Process(Slice key, const Frame &frame) override {
    // Find all Wikidata aliases.
    for (const auto &s : frame) {
      if (s.name != n_profile_alias_) continue;
      Frame alias(frame.store(), s.value);
      if (alias.GetHandle(n_lang_) != language_) continue;
      int sources = alias.GetInt(n_alias_sources_, 0);
      if ((sources & (1 << SRC_WIKIDATA_ALIAS)) == 0) continue;

      // Accumulate counts for alias.
      Text name = alias.GetText(n_name_);
      int count = alias.GetInt(n_alias_count_, 1);
      accumulator_.Increment(name, count);
    }
  }

  void Flush(Task *task) override {
    accumulator_.Flush();
  }

 private:
  // Language for aliases.
  Handle language_;

  // Accumulator for alias counts.
  Accumulator accumulator_;

  // Symbols.
  Name n_name_{names_, "name"};
  Name n_lang_{names_, "lang"};
  Name n_profile_alias_{names_, "/s/profile/alias"};
  Name n_alias_sources_{names_, "/s/alias/sources"};
  Name n_alias_count_{names_, "/s/alias/count"};
};

REGISTER_TASK_PROCESSOR("toxic-alias-mapper", ToxicAliasMapper);

// Output aliases with high ambiguity, i.e. high counts. These aliases are
// usually generic labels or types for the entity rather than actual aliases.
class ToxicAliasReducer : public SumReducer {
 public:
  void Start(Task *task) override {
    SumReducer::Start(task);
    task->Fetch("threshold", &threshold_);
  }

  void Aggregate(int shard, const Slice &key, uint64 sum) override {
    if (sum >= threshold_) {
      LOG(INFO) << "Alias " << key << " count: " << sum;
      Output(shard, new Message(key, SimpleItoa(sum)));
    }
  }

 private:
  int64 threshold_ = 100;
};

REGISTER_TASK_PROCESSOR("toxic-alias-reducer", ToxicAliasReducer);

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up workflow.
  LOG(INFO) << "Set up workflow";
  string wfdir = Corpora::workflow("wikidata");
  string infile = wfdir + "/items@10";
  string outfile = wfdir + "/toxic-aliases-" + FLAGS_language;
  Job wf;
  ResourceFactory rf(&wf);

  // Map reduction for finding toxic aliases in Wikidata.
  MapReduce mr(&wf, "toxic-alias",
               rf.Files(infile, "records/frame"),
               rf.Files(outfile, "textmap"),
               "toxic-alias-mapper",
               "toxic-alias-reducer",
               "text");
  int64 threshold = FLAGS_threshold;
  mr.map.mapper->AddParameter("language", FLAGS_language);
  mr.reduce.reducer->AddParameter("threshold", threshold);

  // Run.
  LOG(INFO) << "Run workflow";
  wf.Run();
  while (!wf.Wait(15000)) wf.DumpCounters();

  wf.Wait();

  LOG(INFO) << "Done workflow";
  wf.DumpCounters();

  return 0;
}


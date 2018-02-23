#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/task/frames.h"
#include "sling/task/job.h"
#include "sling/task/task.h"
#include "sling/workflow/common.h"

DEFINE_string(language, "en", "Wikipedia language");

using namespace sling;
using namespace sling::task;

class WikiIdMapper : public Processor {
 public:
  void Start(Task *task) override {
    // Get output channel.
    output_ = task->GetSink("output");
    CHECK(output_ != nullptr) << "Output channel missing";

    // Initialize commons.
    n_wikipedia_ = commons_.Lookup("/w/item/wikipedia");
    n_language_ = commons_.Lookup("/lang/" + FLAGS_language);
    commons_.Freeze();

    // Statistics.
    num_not_rekeyed_ = task->GetCounter("records_not_rekeyed");
  }

  void Receive(Channel *channel, Message *message) override {
    // Decode profile.
    Store store(&commons_);
    Frame profile = DecodeMessage(&store, message);
    CHECK(profile.valid());

    // Get Wikipedia id for item.
    bool discard = true;
    Frame wikipedia = profile.Get(n_wikipedia_).AsFrame();
    if (wikipedia.valid()) {
      Frame key = wikipedia.Get(n_language_).AsFrame();
      if (key.valid()) {
        // Update key in message.
        message->set_key(key.Id().slice());
        discard = false;
      }
    }

    // Discard messages without Wikipedia id.
    if (discard) {
      delete message;
      num_not_rekeyed_->Increment();
      return;
    }

    // Output message on output channel.
    output_->Send(message);
  }

 private:
  // Output channel.
  Channel *output_ = nullptr;

  // Commons store.
  Store commons_;
  Handle n_wikipedia_;
  Handle n_language_;

  // Statistics.
  Counter *num_not_rekeyed_ = nullptr;
};

REGISTER_TASK_PROCESSOR("wiki-id-mapper", WikiIdMapper);

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up workflow.
  LOG(INFO) << "Set up workflow";
  string wddir = Corpora::workflow("wikidata");
  string wfdir = Corpora::workflow("wikifuse");
  Job wf;
  ResourceFactory rf(&wf);

  // Map reduce over Wikidata for keying by Wikipedia id.
  MapReduce mr(&wf, "wikifuse",
      rf.Files(wddir + "/items@10", "records/wdid:sling"),
      rf.Files(wfdir + "/wikilinks@10", "records/wpid:sling"),
      "wiki-id-mapper", "", "wpid:sling");

  // Run.
  LOG(INFO) << "Run workflow";
  wf.Start();
  while (!wf.Wait(15000)) wf.DumpCounters();
  wf.Wait();

  LOG(INFO) << "Done workflow";
  wf.DumpCounters();

  return 0;
}


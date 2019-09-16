// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sling/task/learner.h"

#include <string>
#include <unordered_map>

#include "sling/frame/store.h"
#include "sling/nlp/document/document.h"
#include "sling/nlp/document/document-corpus.h"
#include "sling/nlp/parser/action-table.h"
#include "sling/nlp/parser/parser-action.h"
#include "sling/nlp/parser/trainer/transition-generator.h"

namespace sling {
namespace nlp {

using namespace task;
using namespace myelin;

// Trainer for transition-based frame-semantic parser.
class ParserTrainer : public LearnerTask {
 public:
  ~ParserTrainer() {
    delete training_corpus_;
    delete evaluation_corpus_;
  }

  // Run training of parser.
  void Run(task::Task *task) override {
    // Open training and evaluation corpora.
    training_corpus_ =
      new DocumentCorpus(&commons_, task->GetInputFiles("training_corpus"));
    evaluation_corpus_ =
      new DocumentCorpus(&commons_, task->GetInputFiles("evaluation_corpus"));

    // Collect word vocabulary and parser actions.
    training_corpus_->Rewind();
    std::unordered_map<string, int> words;
    for (;;) {
      // Get next document.
      Document *document = training_corpus_->Next(&commons_);
      if (document == nullptr) break;

      // Update word vocabulary.
      for (const Token &t : document->tokens()) words[t.word()]++;

      // Update action table.
      LOG(INFO) << "Document: " << document->PhraseText(0, document->length());
      Generate(*document, [&](const ParserAction &action) {
        LOG(INFO) << action.ToString(&commons_);
      });

      delete document;
    }

    LOG(INFO) << "Word vocabulary: " << words.size();
  }

  // Worker thread for training model.
  void Worker(int index, Network *model) override {
  }

  // Evaluate model.
  bool Evaluate(int64 epoch, Network *model) override {
    return true;
  }

  // Checkpoint model.
  void Checkpoint(int64 epoch, Network *model) override {
  }

 private:
  // Commons store for parser.
  Store commons_;

  // Training corpus.
  DocumentCorpus *training_corpus_ = nullptr;

  // Evaluation corpus.
  DocumentCorpus *evaluation_corpus_ = nullptr;

  // Parser action table.
  ActionTable actions_;
};

REGISTER_TASK_PROCESSOR("parser-trainer", ParserTrainer);

}  // namespace nlp
}  // namespace sling


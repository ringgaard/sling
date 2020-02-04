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

#include "sling/nlp/parser/action-table.h"
#include "sling/nlp/parser/multiclass-learner.h"
#include "sling/nlp/parser/parser-trainer.h"
#include "sling/nlp/parser/transition-generator.h"

namespace sling {
namespace nlp {

using namespace task;
using namespace myelin;

// Main delegate for coarse-grained shift/mark/other classification.
class ShiftMarkOtherDelegateLearner : public MultiClassDelegateLearner {
 public:
  ShiftMarkOtherDelegateLearner(int other)
      : MultiClassDelegateLearner("coarse") {
    // Set up coarse actions.
    actions_.Add(ParserAction(ParserAction::SHIFT));
    actions_.Add(ParserAction(ParserAction::MARK));
    actions_.Add(ParserAction(ParserAction::CASCADE, other));
  }
};

// Delegate for fine-grained parser action classification.
class ClassificationDelegateLearner : public MultiClassDelegateLearner {
 public:
  ClassificationDelegateLearner(const ActionTable &actions)
      : MultiClassDelegateLearner("fine") {
    for (const ParserAction &action : actions.list()) {
      actions_.Add(action);
    }
  }
};

// Parser trainer for simple cascaded parser with a coarse-grained main delegate
// for shift and mark and a fine-grained delegate for the rest of the actions.
class CasparTrainer : public ParserTrainer {
 public:
   // Set up caspar parser model.
   void Setup(task::Task *task) override {
    // Get training parameters.
    task->Fetch("max_source", &max_source_);
    task->Fetch("max_target", &max_target_);

    // Reset parser state between sentences.
    sentence_reset_ = true;

    // Collect action vocabularies from training corpus.
    ActionTable actions;
    training_corpus_->Rewind();
    for (;;) {
      // Get next document.
      Document *document = training_corpus_->Next(&commons_);
      if (document == nullptr) break;

      // Generate action table for fine-grained classifier.
      Generate(*document, [&](const ParserAction &action) {
        bool skip = false;
        switch (action.type) {
          case ParserAction::SHIFT:
          case ParserAction::MARK:
            skip = true;
            break;
          case ParserAction::CONNECT:
            if (action.source > max_source_) skip = true;
            if (action.target > max_target_) skip = true;
            break;
          case ParserAction::ASSIGN:
            if (action.source > max_source_) skip = true;
            break;
          default:
            break;
        }
        if (!skip) actions.Add(action);
      });

      delete document;
    }
    roles_.Add(actions.list());

    // Set up delegates.
    delegates_.push_back(new ShiftMarkOtherDelegateLearner(1));
    delegates_.push_back(new ClassificationDelegateLearner(actions));
  }

  // Transition generator.
  void GenerateTransitions(const Document &document, int begin, int end,
                           std::vector<ParserAction> *transitions) override {
    transitions->clear();
    Generate(document, begin, end, [&](const ParserAction &action) {
      if (action.type != ParserAction::SHIFT &&
          action.type != ParserAction::MARK) {
        transitions->emplace_back(ParserAction::CASCADE, 1);
      }
      transitions->push_back(action);
    });
  }

 private:
  // Hyperparameters.
  int max_source_ = 5;
  int max_target_ = 10;
};

REGISTER_TASK_PROCESSOR("caspar-trainer", CasparTrainer);

}  // namespace nlp
}  // namespace sling


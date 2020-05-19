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

#include "sling/nlp/document/document-corpus.h"
#include "sling/nlp/parser/action-table.h"
#include "sling/nlp/parser/multiclass-delegate.h"
#include "sling/nlp/parser/transition-decoder.h"
#include "sling/nlp/parser/transition-generator.h"

namespace sling {
namespace nlp {

using namespace task;
using namespace myelin;

// Main delegate for coarse-grained shift/mark/other classification.
class ShiftMarkOtherDelegate : public MultiClassDelegate {
 public:
  ShiftMarkOtherDelegate(int other) : MultiClassDelegate("coarse") {
    // Set up coarse actions.
    actions_.Add(ParserAction(ParserAction::SHIFT));
    actions_.Add(ParserAction(ParserAction::MARK));
    actions_.Add(ParserAction(ParserAction::CASCADE, other));
  }
};

// Delegate for fine-grained parser action classification.
class ClassificationDelegate : public MultiClassDelegate {
 public:
  ClassificationDelegate(const ActionTable &actions)
      : MultiClassDelegate("fine") {
    for (const ParserAction &action : actions.list()) {
      actions_.Add(action);
    }
  }
};

// Parser decoder for simple cascaded parser with a coarse-grained main delegate
// for shift and mark and a fine-grained delegate for the rest of the actions.
class CasparDecoder : public TransitionDecoder {
 public:
   // Set up caspar parser model.
   void Setup(task::Task *task, Store *commons) override {
    // Set up transition decoder.
    TransitionDecoder::Setup(task, commons);

    // Get training parameters.
    task->Fetch("max_source", &max_source_);
    task->Fetch("max_target", &max_target_);

    // Reset parser state between sentences.
    sentence_reset_ = true;

    // Collect action vocabularies from training corpus.
    DocumentCorpus corpus(commons, task->GetInputFiles("training_corpus"));
    ActionTable actions;
    for (;;) {
      // Get next document.
      Document *document = corpus.Next(commons);
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
    delegates_.push_back(new ShiftMarkOtherDelegate(1));
    delegates_.push_back(new ClassificationDelegate(actions));
  }

  // Transition generator.
  void GenerateTransitions(const Document &document, int begin, int end,
                           Transitions *transitions) const override {
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

REGISTER_PARSER_DECODER("caspar", CasparDecoder);

}  // namespace nlp
}  // namespace sling


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

#include "sling/myelin/builder.h"
#include "sling/myelin/flow.h"
#include "sling/myelin/gradient.h"
#include "sling/myelin/learning.h"
#include "sling/nlp/parser/action-table.h"
#include "sling/nlp/parser/parser-trainer.h"
#include "sling/nlp/parser/trainer/transition-generator.h"

namespace sling {
namespace nlp {

using namespace task;
using namespace myelin;

// Deletegate for fixed action classification using a softmax cross-entropy
// loss.
class MultiClassDelegateLearner : public DelegateLearner {
 public:
  MultiClassDelegateLearner(const string &name)
      : name_(name), loss_(name + "_loss") {}

  void Build(Flow *flow, Library *library,
             Flow::Variable *activations,
             Flow::Variable *dactivations) override {
    FlowBuilder f(flow, name_);
    int dim = activations->elements();
    int size = actions_.size();
    auto *input = f.Placeholder("input", DT_FLOAT, {1, dim}, true);
    auto *W = f.Random(f.Parameter("W", DT_FLOAT, {dim, size}));
    auto *b = f.Random(f.Parameter("b", DT_FLOAT, {1, size}));
    auto *logits = f.Name(f.Add(f.MatMul(input, W), b), "logits");

    flow->Connect({activations, input});
    if (library != nullptr) {
      Gradient(flow, f.func(), *library);
      auto *dlogits = flow->GradientVar(logits);
      loss_.Build(flow, logits, dlogits);
    }
  }

  void Initialize(const Network &network) override {
    cell_ = network.GetCell(name_);
    input_ = cell_->GetParameter(name_ + "/input");
    logits_ = cell_->GetParameter(name_ + "/logits");

    dcell_ = cell_->Gradient();
    dinput_ = input_->Gradient();
    dlogits_ = logits_->Gradient();
  }

  DelegateLearnerInstance *Instance() override {
    // TODO: implement MultiClassDelegateLearnerInstance.
    return nullptr;
  }

 protected:
  string name_;                // delegate name
  ActionTable actions_;        // action table for multi-class classification
  CrossEntropyLoss loss_;      // loss function

  Cell *cell_ = nullptr;       // cell for forward computation
  Tensor *input_ = nullptr;    // input for activations
  Tensor *logits_ = nullptr;   // output with logits

  Cell *dcell_ = nullptr;      // cell for backward computation
  Tensor *dinput_ = nullptr;   // gradient for activations
  Tensor *dlogits_ = nullptr;  // gradient for logits
};

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
    // Collect word and action vocabularies from training corpus.
    training_corpus_->Rewind();
    for (;;) {
      // Get next document.
      Document *document = training_corpus_->Next(&commons_);
      if (document == nullptr) break;

      // Update word vocabulary.
      for (const Token &t : document->tokens()) words_[t.word()]++;

      // Generate action table for fine-grained classifier.
      Generate(*document, [&](const ParserAction &action) {
        bool skip = false;
        switch (action.type) {
          case ParserAction::SHIFT:
          case ParserAction::MARK:
            skip = true;
            return;
          case ParserAction::CONNECT:
            if (action.source > max_source_) skip = true;
            if (action.target > max_target_) skip = true;
            break;
          case ParserAction::ASSIGN:
          case ParserAction::EMBED:
          case ParserAction::ELABORATE:
            if (action.source > max_source_) skip = true;
            break;
          default:
            break;
        }
        if (!skip) actions_.Add(action);
      });

      delete document;
    }
    roles_.Init(actions_.list());

    // Set up delegates.
    delegates_.push_back(new ShiftMarkOtherDelegateLearner(1));
    delegates_.push_back(new ClassificationDelegateLearner(actions_));
  }

 private:
  // Parser actions.
  ActionTable actions_;
};

REGISTER_TASK_PROCESSOR("caspar-trainer", CasparTrainer);

}  // namespace nlp
}  // namespace sling


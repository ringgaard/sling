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
    // Get training parameters.
    task->Fetch("max_source", &max_source_);
    task->Fetch("max_target", &max_target_);

    // Open training and evaluation corpora.
    training_corpus_ =
      new DocumentCorpus(&commons_, task->GetInputFiles("training_corpus"));
    evaluation_corpus_ =
      new DocumentCorpus(&commons_, task->GetInputFiles("evaluation_corpus"));

    // Build word and action vocabularies.
    BuildVocabularies();
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

  // Build word and action vocabulary.
  void BuildVocabularies() {
    // Collect vocabularies from training corpus.
    training_corpus_->Rewind();
    for (;;) {
      // Get next document.
      Document *document = training_corpus_->Next(&commons_);
      if (document == nullptr) break;

      // Update word vocabulary.
      for (const Token &t : document->tokens()) AddWord(t.word());

      // Update action table.
      Generate(*document, [&](const ParserAction &action) {
        AddAction(action);
      });

      delete document;
    }

    LOG(INFO) << "Word vocabulary: " << words_.size();
    LOG(INFO) << "Action vocabulary: " << action_table_.size();
  }

  // Add word to word vocabulary.
  void AddWord(const string &word) {
    words_[word]++;
  }

  // Add action to action vocabulary if it is within context bounds.
  void AddAction(const ParserAction &action) {
    // Check context bounds.
    switch (action.type) {
      case ParserAction::CONNECT:
        if (action.source > max_source_) return;
        if (action.target > max_target_) return;
        break;
      case ParserAction::ASSIGN:
      case ParserAction::EMBED:
      case ParserAction::ELABORATE:
        if (action.source > max_source_) return;
        break;
      default:
        break;
    }

    // Check if action is already in vocabulary.
    if (action_map_.find(action) != action_map_.end()) return;

    // Add action to action index and mapping.
    int index = action_table_.size();
    action_table_.push_back(action);
    action_map_[action] = index;
  }

 private:
  // Commons store for parser.
  Store commons_;

  // Training corpus.
  DocumentCorpus *training_corpus_ = nullptr;

  // Evaluation corpus.
  DocumentCorpus *evaluation_corpus_ = nullptr;

  // Word vocabulary.
  std::unordered_map<string, int> words_;

  // Parser action table.
  std::vector<ParserAction> action_table_;
  std::unordered_map<ParserAction, int, ParserActionHash> action_map_;

  // Hyper-parameters.
  int max_source_ = 5;
  int max_target_ = 10;
};

REGISTER_TASK_PROCESSOR("parser-trainer", ParserTrainer);

}  // namespace nlp
}  // namespace sling


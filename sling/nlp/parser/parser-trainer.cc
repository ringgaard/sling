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
#include "sling/myelin/builder.h"
#include "sling/myelin/compiler.h"
#include "sling/myelin/gradient.h"
#include "sling/myelin/learning.h"
#include "sling/nlp/document/document.h"
#include "sling/nlp/document/document-corpus.h"
#include "sling/nlp/document/lexical-encoder.h"
#include "sling/nlp/document/lexicon.h"
#include "sling/nlp/parser/parser-action.h"
#include "sling/nlp/parser/trainer/transition-generator.h"
#include "sling/util/unicode.h"

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
    task->Fetch("lstm_dim", &lstm_dim_);
    task->Fetch("max_source", &max_source_);
    task->Fetch("max_target", &max_target_);
    task->Fetch("mark_depth", &mark_depth_);
    task->Fetch("frame_limit", &frame_limit_);
    task->Fetch("attention_depth", &attention_depth_);
    task->Fetch("history_size", &history_size_);
    task->Fetch("out_roles_size", &out_roles_size_);
    task->Fetch("in_roles_size", &in_roles_size_);
    task->Fetch("labeled_roles_size", &labeled_roles_size_);
    task->Fetch("unlabeled_roles_size", &unlabeled_roles_size_);

    // Open training and evaluation corpora.
    training_corpus_ =
      new DocumentCorpus(&commons_, task->GetInputFiles("training_corpus"));
    evaluation_corpus_ =
      new DocumentCorpus(&commons_, task->GetInputFiles("evaluation_corpus"));

    // Set up encoder lexicon.
    string normalization = task->Get("normalization", "d");
    spec_.lexicon.normalization = ParseNormalization(normalization);
    spec_.lexicon.threshold = task->Get("lexicon_threshold", 0);
    spec_.lexicon.max_prefix = task->Get("max_prefix", 0);
    spec_.lexicon.max_suffix = task->Get("max_suffix", 3);

    // Set up word embeddings.
    spec_.word_dim = task->Get("word_dim", 32);
    spec_.word_embeddings = task->GetInputFile("word_embeddings");
    spec_.train_word_embeddings = task->Get("train_word_embeddings", true);

    // Set up lexical bakc-off features.
    spec_.prefix_dim = task->Get("prefix_dim", 0);
    spec_.suffix_dim = task->Get("suffix_dim", 16);
    spec_.hyphen_dim = task->Get("hypen_dim", 8);
    spec_.caps_dim = task->Get("caps_dim", 8);;
    spec_.punct_dim = task->Get("punct_dim", 8);;
    spec_.quote_dim = task->Get("quote_dim", 8);;
    spec_.digit_dim = task->Get("digit_dim", 8);;

    // Build word and action vocabularies.
    BuildVocabularies();

    // Build parser model flow graph.
    BuildFlow(&flow_, true);

    // Compile model.
    compiler_.Compile(&flow_, &net_);

    // Initialize model.
    encoder_.Initialize(net_);
    //model_.Initialize(net_);
    //loss_.Initialize(net_);
    //optimizer_->Initialize(net_);
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

  // Build flow graph for parser model.
  void BuildFlow(Flow *flow, bool learn) {
    // Build document input encoder.
    Library *library = compiler_.library();
    BiLSTM::Outputs lstm;
    if (learn) {
      Vocabulary::HashMapIterator vocab(words_);
      lstm = encoder_.Build(flow, *library, spec_, &vocab, lstm_dim_, true);
    } else {
      lstm = encoder_.Build(flow, *library, spec_, nullptr, lstm_dim_, false);
    }
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

  // Parser actions.
  std::vector<ParserAction> action_table_;
  std::unordered_map<ParserAction, int, ParserActionHash> action_map_;

  // Lexical feature specification for encoder.
  LexicalFeatures::Spec spec_;

  // Neural network.
  Flow flow_;
  Network net_;
  Compiler compiler_;

  // Document input encoder.
  LexicalEncoder encoder_;

  // Model dimensions.
  int lstm_dim_ = 256;
  int max_source_ = 5;
  int max_target_ = 10;
  int mark_depth_ = 1;
  int frame_limit_ = 5;
  int attention_depth_ = 5;
  int history_size_ = 5;
  int out_roles_size_ = 16;
  int in_roles_size_ = 16;
  int labeled_roles_size_ = 16;
  int unlabeled_roles_size_ = 16;
  std::vector<int> mark_distance_bins_{0, 1, 2, 3, 6, 10, 15, 20};
};

REGISTER_TASK_PROCESSOR("parser-trainer", ParserTrainer);

}  // namespace nlp
}  // namespace sling


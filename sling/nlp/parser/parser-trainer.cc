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
#include "sling/nlp/parser/action-table.h"
#include "sling/nlp/parser/parser-action.h"
#include "sling/nlp/parser/parser-features.h"
#include "sling/nlp/parser/roles.h"
#include "sling/nlp/parser/trainer/transition-generator.h"
#include "sling/util/unicode.h"

namespace sling {
namespace nlp {

using namespace task;
using namespace myelin;

// Interface for delegate learner.
class DelegateLearner {
 public:
  virtual ~DelegateLearner() = default;

  // Build flow for delegate learner.
  virtual void Build(Flow *flow, Library *library,
                     Flow::Variable *activations,
                     Flow::Variable *dactivations) = 0;
};

// Main delegate for coarse-grained shift/mark/other classification.
class ShiftMarkOtherDelegateLearner : public DelegateLearner {
 public:
  ShiftMarkOtherDelegateLearner(int other) {
    actions_.emplace_back(ParserAction::SHIFT);
    actions_.emplace_back(ParserAction::MARK);
    actions_.emplace_back(ParserAction::CASCADE, other);
  }

  void Build(Flow *flow, Library *library,
             Flow::Variable *activations,
             Flow::Variable *dactivations) override {
    FlowBuilder f(flow, "coarse");
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

 private:
  std::vector<ParserAction> actions_;
  CrossEntropyLoss loss_{"coarse_loss"};
};

// Delegate for fine-grained parser action classification.
class ClassificationDelegateLearner : public DelegateLearner {
 public:
  ClassificationDelegateLearner(const ActionTable &actions)
      : actions_(actions) {}

  void Build(Flow *flow, Library *library,
             Flow::Variable *activations,
             Flow::Variable *dactivations) override {

  }

 private:
  const ActionTable &actions_;
};

// Trainer for transition-based frame-semantic parser.
class ParserTrainer : public LearnerTask {
 public:
  ~ParserTrainer() {
    for (auto *d : delegates_) delete d;
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
    task->Fetch("roles_dim", &roles_dim_);
    task->Fetch("activations_dim", &activations_dim_);
    task->Fetch("link_dim_lstm", &link_dim_lstm_);
    task->Fetch("link_dim_ff", &link_dim_ff_);
    task->Fetch("mark_dim", &mark_dim_);

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
    spec_.feature_padding = 16;

    // Set up word embeddings.
    spec_.word_dim = task->Get("word_dim", 32);
    spec_.word_embeddings = task->GetInputFile("word_embeddings");
    spec_.train_word_embeddings = task->Get("train_word_embeddings", true);

    // Set up lexical back-off features.
    spec_.prefix_dim = task->Get("prefix_dim", 0);
    spec_.suffix_dim = task->Get("suffix_dim", 16);
    spec_.hyphen_dim = task->Get("hypen_dim", 8);
    spec_.caps_dim = task->Get("caps_dim", 8);;
    spec_.punct_dim = task->Get("punct_dim", 8);;
    spec_.quote_dim = task->Get("quote_dim", 8);;
    spec_.digit_dim = task->Get("digit_dim", 8);;

    // Build word and action vocabularies.
    BuildVocabularies();

    // Set up delegates.
    InitDelegates();

    // Build parser model flow graph.
    BuildFlow(&flow_, true);
    optimizer_ = GetOptimizer(task);
    optimizer_->Build(&flow_);

    // Compile model.
    compiler_.Compile(&flow_, &net_);

    // Initialize model.
    feature_model_.Init(net_.GetCell("ff_trunk"),
                        flow_.DataBlock("spec"),
                        &roles_, frame_limit_);
    encoder_.Initialize(net_);
    optimizer_->Initialize(net_);

    delete optimizer_;
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
    roles_.Init(actions_.actions());

    LOG(INFO) << "Word vocabulary: " << words_.size();
    LOG(INFO) << "Action vocabulary: " << actions_.NumActions();
    LOG(INFO) << "Role set: " << roles_.size();

  }

  // Add word to word vocabulary.
  void AddWord(const string &word) {
    words_[word]++;
  }

  // Add action to action vocabulary if it is within context bounds.
  void AddAction(const ParserAction &action) {
    // Check context bounds.
    switch (action.type) {
      case ParserAction::SHIFT:
      case ParserAction::MARK:
        return;
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

    actions_.Add(action);
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

    // Build parser decoder.
    FlowBuilder f(flow, "ff_trunk");
    std::vector<Flow::Variable *> features;
    Flow::Blob *spec = flow->AddBlob("spec", "");

    // Add inputs for recurrent channels.
    auto *lr = f.Placeholder("link/lr_lstm", DT_FLOAT, {1, lstm_dim_}, true);
    auto *rl = f.Placeholder("link/rl_lstm", DT_FLOAT, {1, lstm_dim_}, true);
    auto *steps = f.Placeholder("steps", DT_FLOAT, {1, activations_dim_}, true);

    // Role features.
    if (in_roles_size_ > 0) {
      features.push_back(f.Feature("in-roles", roles_.size() * frame_limit_,
                                   in_roles_size_, roles_dim_));
    }
    if (out_roles_size_ > 0) {
      features.push_back(f.Feature("out-roles", roles_.size() * frame_limit_,
                                   out_roles_size_, roles_dim_));
    }
    if (labeled_roles_size_ > 0) {
      features.push_back(f.Feature("labeled-roles",
                                   roles_.size() * frame_limit_ * frame_limit_,
                                   labeled_roles_size_, roles_dim_));
    }
    if (unlabeled_roles_size_ > 0) {
      features.push_back(f.Feature("unlabeled-roles",
                                   frame_limit_ * frame_limit_,
                                   unlabeled_roles_size_, roles_dim_));
    }


    // Link features.
    features.push_back(LinkedFeature(&f, "frame-creation-steps",
                                     steps, frame_limit_, link_dim_ff_));
    features.push_back(LinkedFeature(&f, "frame-focus-steps",
                                     steps, frame_limit_, link_dim_ff_));
    features.push_back(LinkedFeature(&f, "history",
                                     steps, history_size_, link_dim_ff_));
    features.push_back(LinkedFeature(&f, "frame-end-lr",
                                     lr, frame_limit_, link_dim_lstm_));
    features.push_back(LinkedFeature(&f, "frame-end-rl",
                                     rl, frame_limit_, link_dim_lstm_));
    features.push_back(LinkedFeature(&f, "lr", lr, 1, link_dim_lstm_));
    features.push_back(LinkedFeature(&f, "rl", rl, 1, link_dim_lstm_));

    // Mark features.
    features.push_back(f.Feature("mark-distance",
                                 mark_distance_bins_.size() + 1,
                                 mark_depth_, mark_dim_));
    features.push_back(LinkedFeature(&f, "mark-lr",
                                     lr, mark_depth_, link_dim_lstm_));
    features.push_back(LinkedFeature(&f, "mark-rl",
                                     rl, mark_depth_, link_dim_lstm_));
    features.push_back(LinkedFeature(&f, "mark-step",
                                     steps, mark_depth_, link_dim_ff_));
    string bins;
    for (int d : mark_distance_bins_) {
      if (!bins.empty()) bins.push_back(' ');
      bins.append(std::to_string(d));
    }
    spec->SetAttr("mark_distance_bins", bins);

    // Concatenate mapped feature inputs.
    auto *fv = f.Concat(features);
    int fvsize = fv->dim(1);

    // Feed-forward layer.
    auto *W = f.Random(f.Parameter("W0", DT_FLOAT, {fvsize, activations_dim_}));
    auto *b = f.Random(f.Parameter("b0", DT_FLOAT, {1, activations_dim_}));
    auto *activations = f.Name(f.Relu(f.Add(f.MatMul(fv, W), b)), "hidden");
    activations->set_in()->set_out()->set_ref();

    // Build function decoder gradient.
    Flow::Variable *dactivations = nullptr;
    if (learn) {
      Gradient(flow, f.func(), *library);
      dactivations = flow->GradientVar(activations);
    }

    // Build flows for delegates.
    for (DelegateLearner *delegate : delegates_) {
      delegate->Build(flow, library, activations, dactivations);
    }

    // Link recurrences.
    flow->Connect({lstm.lr, lr});
    flow->Connect({lstm.rl, rl});
    flow->Connect({steps, activations});
    if (learn) {
      auto *dsteps = flow->GradientVar(steps);
      flow->Connect({dsteps, dactivations});
    }
  }

  // Build linked feature.
  static Flow::Variable *LinkedFeature(FlowBuilder *f,
                                       const string &name,
                                       Flow::Variable *embeddings,
                                       int size, int dim) {
    int link_dim = embeddings->dim(1);
    auto *features = f->Placeholder(name, DT_INT32, {1, size});
    auto *oov = f->Parameter(name + "_oov", DT_FLOAT, {1, link_dim});
    auto *gather = f->Gather(embeddings, features, oov);
    auto *transform = f->Parameter(name + "_transform", DT_FLOAT,
                                   {link_dim, dim});
    return f->Reshape(f->MatMul(gather, transform), {1, size * dim});
  }

  void InitDelegates() {
    delegates_.push_back(new ShiftMarkOtherDelegateLearner(1));
    delegates_.push_back(new ClassificationDelegateLearner(actions_));
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
  ActionTable actions_;

  // Role set.
  RoleSet roles_;

  // Lexical feature specification for encoder.
  LexicalFeatures::Spec spec_;

  // Neural network.
  Flow flow_;
  Network net_;
  Compiler compiler_;
  Optimizer *optimizer_ = nullptr;

  // Document input encoder.
  LexicalEncoder encoder_;

  // Parser feature model.
  ParserFeatureModel feature_model_;

  // Delegates.
  std::vector<DelegateLearner *> delegates_;

  // Model dimensions.
  int lstm_dim_ = 256;
  int max_source_ = 5;
  int max_target_ = 10;
  int mark_depth_ = 1;
  int frame_limit_ = 5;
  int attention_depth_ = 5;
  int history_size_ = 5;
  int out_roles_size_ = 32;
  int in_roles_size_ = 32;
  int labeled_roles_size_ = 32;
  int unlabeled_roles_size_ = 32;
  int roles_dim_ = 16;
  int activations_dim_ = 128;
  int link_dim_lstm_ = 32;
  int link_dim_ff_ = 64;
  int mark_dim_ = 32;
  std::vector<int> mark_distance_bins_{0, 1, 2, 3, 6, 10, 15, 20};
};

REGISTER_TASK_PROCESSOR("parser-trainer", ParserTrainer);

}  // namespace nlp
}  // namespace sling


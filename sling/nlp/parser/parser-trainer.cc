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

#include "sling/nlp/parser/parser-trainer.h"

#include "sling/myelin/gradient.h"
#include "sling/nlp/document/document.h"
#include "sling/nlp/document/lexicon.h"
#include "sling/util/unicode.h"

namespace sling {
namespace nlp {

using namespace task;
using namespace myelin;

ParserTrainer::~ParserTrainer() {
  for (auto *d : delegates_) delete d;
  delete training_corpus_;
  delete evaluation_corpus_;
}

void ParserTrainer::Run(task::Task *task) {
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

  // Custom parser model initialization. This should set up the word and role
  // vocabularies as well as the delegate cascade.
  Setup(task);

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
  for (auto *d : delegates_) d->Initialize(net_);
  commons_.Freeze();

  // Clean up.
  delete optimizer_;
}

void ParserTrainer::Worker(int index, Network *model) {
}

bool ParserTrainer::Evaluate(int64 epoch, Network *model) {
  return true;
}

void ParserTrainer::Checkpoint(int64 epoch, Network *model) {
}

void ParserTrainer::BuildFlow(Flow *flow, bool learn) {
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

Flow::Variable *ParserTrainer::LinkedFeature(FlowBuilder *f,
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

Document *ParserTrainer::GetNextTrainingDocument() {
  MutexLock lock(&mu_);
  Document *document = training_corpus_->Next(&commons_);
  if (document == nullptr) {
    // Loop around when end of training corpus reached.
    training_corpus_->Rewind();
    document = training_corpus_->Next(&commons_);
  }
  return document;
}

}  // namespace nlp
}  // namespace sling


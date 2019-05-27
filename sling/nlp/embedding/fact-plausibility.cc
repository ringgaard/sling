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

#include "sling/file/textmap.h"
#include "sling/frame/object.h"
#include "sling/frame/serialization.h"
#include "sling/frame/store.h"
#include "sling/myelin/compiler.h"
#include "sling/myelin/builder.h"
#include "sling/myelin/gradient.h"
#include "sling/myelin/learning.h"
#include "sling/task/frames.h"
#include "sling/task/learner.h"
#include "sling/util/random.h"

namespace sling {
namespace nlp {

using namespace task;
using namespace myelin;

// Fact plausibility flow.
struct FactPlausibilityFlow : public Flow {
  // Build fact plausibility model.
  void Build(const Transformations &library) {
    scorer = AddFunction("scorer");
    FlowBuilder f(this, scorer);
    auto *embeddings =
        f.Random(f.Parameter("embeddings", DT_FLOAT, {facts, dims}));

    premise = f.Placeholder("premise", DT_INT32, {1, max_features});
    auto *pencoding = f.GatherSum(embeddings, premise);

    hypothesis = f.Placeholder("hypothesis", DT_INT32, {1, max_features});
    auto *hencoding = f.GatherSum(embeddings, hypothesis);

    auto *fv = f.Concat({pencoding, hencoding});
    logits = f.Name(f.FFLayers(fv, {dims * 2, 2}, -1, true, "Relu"), "logits");

    // Create gradient computations.
    gscorer = Gradient(this, scorer, library);
    d_logits = GradientVar(logits);
    primal = PrimalVar(scorer);
  }

  int facts = 1;                   // number of fact types
  int dims = 64;                   // dimension of embedding vectors
  int max_features = 512;          // maximum number of features per example

  Function *scorer;                // plausibility scoring function
  Function *gscorer;               // plausibility scoring gradient function
  Variable *premise;               // premise facts
  Variable *hypothesis;            // hypothesis facts
  Variable *logits;                // plausibility prediction
  Variable *d_logits;              // plausibility gradient
  Variable *primal;                // primal reference for scorer
};

// Trainer for fact plausibility model.
class FactPlausibilityTrainer : public LearnerTask {
 public:
  // Run training of embedding net.
  void Run(task::Task *task) override {
    // Get training parameters.
    task->Fetch("embedding_dims", &embedding_dims_);
    task->Fetch("batch_size", &batch_size_);
    task->Fetch("batches_per_update", &batches_per_update_);
    task->Fetch("min_facts", &min_facts_);
    task->Fetch("max_features", &max_features_);
    task->Fetch("learning_rate", &learning_rate_);
    task->Fetch("min_learning_rate", &min_learning_rate_);

    // Set up counters.
    Counter *num_instances = task->GetCounter("instances");
    Counter *num_instances_skipped = task->GetCounter("instances_skipped");
    num_feature_overflows_ = task->GetCounter("feature_overflows");

    // Bind names.
    names_.Bind(&store_);

    // Read fact lexicon.
    TextMapInput factmap(task->GetInputFile("factmap"));
    Handles facts(&store_);
    while (factmap.Next()) {
      Object fact = FromText(&store_, factmap.key());
      facts.push_back(fact.handle());
    }
    fact_lexicon_ = Array(&store_, facts);
    task->GetCounter("facts")->Increment(fact_lexicon_.length());

    // Build plausibility model.
    Build(&flow_);
    loss_.Build(&flow_, flow_.logits, flow_.d_logits);
    optimizer_ = GetOptimizer(task);
    optimizer_->Build(&flow_);

    // Compile plausibility model.
    Network model;
    compiler_.Compile(&flow_, &model);
    optimizer_->Initialize(model);
    loss_.Initialize(model);

    // Initialize weights.
    model.InitLearnableWeights(task->Get("seed", 0), 0.0, 0.01);

    // Read training instances from input.
    LOG(INFO) << "Reading training data";
    Queue input(this, task->GetSources("input"));
    Message *message;
    while (input.Read(&message)) {
      // Parse message into frame.
      Frame instance = DecodeMessage(&store_, message);
      Array groups = instance.Get(p_groups_).AsArray();
      if (groups.length() >= min_facts_) {
        instances_.push_back(instance.handle());
        num_instances->Increment();
      } else {
        num_instances_skipped->Increment();
      }

      delete message;
    }
    store_.Freeze();

    // Run training.
    LOG(INFO) << "Starting training";
    Train(task, &model);

    // Output profile.
    LogProfile(model);

    delete optimizer_;
  }

  // Add plausibility model to flow.
  void Build(FactPlausibilityFlow *flow) {
    flow->facts = fact_lexicon_.length();
    flow->dims = embedding_dims_;
    flow->max_features = max_features_;
    flow->Build(*compiler_.library());
  }

  // Worker thread for training embedding model.
  void Worker(int index, Network *model) override {
    // Initialize random number generator.
    Random rnd;
    rnd.seed(index);

    // Premises and hypotheses for one batch.
    std::vector<std::vector<int>> premises(batch_size_);
    std::vector<std::vector<int>> hypotheses(batch_size_);

    // Set up plausibility scorer.
    Instance scorer(flow_.scorer);
    Instance gscorer(flow_.gscorer);
    float *logits = scorer.Get<float>(flow_.logits);
    float *dlogits = gscorer.Get<float>(flow_.d_logits);
    std::vector<myelin::Instance *> gradients{&gscorer};

    for (;;) {
      // Compute gradients for epoch.
      gscorer.Clear();
      gscorer.Set(flow_.primal, &scorer);
      float epoch_loss = 0.0;
      int pos_correct = 0;
      int pos_wrong = 0;
      int neg_correct = 0;
      int neg_wrong = 0;

      for (int b = 0; b < batches_per_update_; ++b) {
        // Random sample instances for batch.
        for (int i = 0; i < batch_size_; ++i) {
          int sample = rnd.UniformInt(instances_.size());
          Frame instance(&store_, instances_[sample]);
          Array facts = instance.Get(p_facts_).AsArray();
          Array groups = instance.Get(p_groups_).AsArray();
          int num_groups = groups.length();

          // Add facts to premise, except for one heldout fact group which is
          // added to the hypothesis.
          auto &premise = premises[i];
          auto &hypothesis = hypotheses[i];
          premise.clear();
          hypothesis.clear();
          int heldout = rnd.UniformInt(num_groups);
          for (int g = 0; g < num_groups; ++g) {
            // Get range for fact group.
            int start = g == 0 ? 0 : groups.get(g - 1).AsInt();
            int end = groups.get(g).AsInt();

            if (g == heldout) {
              // Add fact group to hyothesis.
              for (int f = start; f < end; ++f) {
                hypothesis.push_back(facts.get(f).AsInt());
              }
            } else {
              // Add fact group to premise.
              for (int f = start; f < end; ++f) {
                premise.push_back(facts.get(f).AsInt());
              }
            }
          }

          if (premise.size() > max_features_) {
            premise.resize(max_features_);
          }
          if (hypothesis.size() > max_features_) {
            hypothesis.resize(max_features_);
          }
        }

        // Do forward and backward propagation for each premise/hypothesis pair.
        // Each sampled item is a positive example. Negative examples are
        // generated by using the premise from one item and the hypothsis from
        // another item.
        for (int i = 0; i < batch_size_; ++i) {
          for (int j = 0; j < batch_size_; ++j) {
            // Set premise and hypothesis for example.
            int *p = scorer.Get<int>(flow_.premise);
            int *pend = p + max_features_;
            for (int f : premises[i]) *p++ = f;
            if (p < pend) *p = -1;

            int *h = scorer.Get<int>(flow_.hypothesis);
            int *hend = h + max_features_;
            for (int f : hypotheses[j]) *h++ = f;
            if (h < hend) *h = -1;

            // Compute plausibility scores.
            scorer.Compute();

            // Compute accuracy.
            if (i == j) {
              // Positive example.
              if (logits[1] > logits[0]) {
                pos_correct++;
              } else {
                pos_wrong++;
              }
            } else {
              // Negative example.
              if (logits[0] > logits[1]) {
                neg_correct++;
              } else {
                neg_wrong++;
              }
            }

            // Compute loss.
            int label = i == j ? 1 : 0;
            float loss = loss_.Compute(logits, label, dlogits);
            epoch_loss += loss;

            // Backpropagate.
            gscorer.Compute();
          }
        }
      }

      // Update parameters.
      optimizer_mu_.Lock();
      optimizer_->Apply(gradients);
      loss_sum_ += epoch_loss;
      positive_correct_ += pos_correct;
      positive_wrong_ += pos_wrong;
      negative_correct_ += neg_correct;
      negative_wrong_ += neg_wrong;
      loss_count_ += batches_per_update_ * batch_size_ * batch_size_;
      optimizer_mu_.Unlock();

      // Check if we are done.
      if (EpochCompleted()) break;
    }
  }

  // Evaluate model.
  bool Evaluate(int64 epoch, myelin::Network *model) override {
    // Skip evaluation if there are no data.
    if (loss_count_ == 0) return true;

    // Compute average loss of epochs since last eval.
    float loss = loss_sum_ / loss_count_;
    float p = exp(-loss) * 100.0;
    loss_sum_ = 0.0;
    loss_count_ = 0;

    // Compute accuracy for positive and negative examples.
    float pos_total = positive_correct_ + positive_wrong_;
    float pos_accuracy = (positive_correct_ / pos_total) * 100.0;
    float neg_total = negative_correct_ + negative_wrong_;
    float neg_accuracy = (negative_correct_ / neg_total) * 100.0;
    positive_correct_ = 0;
    positive_wrong_ = 0;
    negative_correct_ = 0;
    negative_wrong_ = 0;

    // Decay learning rate if loss increases.
    if (prev_loss_ != 0.0 &&
        prev_loss_ < loss &&
        learning_rate_ > min_learning_rate_) {
      learning_rate_ = optimizer_->DecayLearningRate();
    }
    prev_loss_ = loss;

    LOG(INFO) << "epoch=" << epoch
              << ", lr=" << learning_rate_
              << ", loss=" << loss
              << ", p=" << p
              << ", +acc=" << pos_accuracy
              << ", -acc=" << neg_accuracy;
    return true;
  }

 private:
  // Training parameters.
  int embedding_dims_ = 256;           // size of embedding vectors
  int min_facts_ = 2;                  // minimum number of facts for example
  int max_features_ = 512;             // maximum features per item
  int batch_size_ = 1024;              // number of examples per batch
  int batches_per_update_ = 1;         // number of batches per epoch

  // Store for training instances.
  Store store_;

  // Fact lexicon.
  Array fact_lexicon_;

  // Evaluation statistics.
  float learning_rate_ = 1.0;
  float min_learning_rate_ = 0.01;

  // Flow model for fact plausibility trainer.
  FactPlausibilityFlow flow_;
  myelin::Compiler compiler_;
  myelin::CrossEntropyLoss loss_;
  myelin::Optimizer *optimizer_ = nullptr;

  // Training instances.
  Handles instances_{&store_};

  // Mutex for serializing access to optimizer.
  Mutex optimizer_mu_;

  float prev_loss_ = 0.0;
  float loss_sum_ = 0.0;
  int loss_count_ = 0.0;
  int positive_correct_ = 0;
  int positive_wrong_ = 0;
  int negative_correct_ = 0;
  int negative_wrong_ = 0;

  // Symbols.
  Names names_;
  Name p_item_{names_, "item"};
  Name p_facts_{names_, "facts"};
  Name p_groups_{names_, "groups"};

  // Statistics.
  Counter *num_feature_overflows_ = nullptr;
};

REGISTER_TASK_PROCESSOR("fact-plausibility-trainer", FactPlausibilityTrainer);

}  // namespace nlp
}  // namespace sling


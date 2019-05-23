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

#if 0
#include <math.h>
#include <atomic>
#include <utility>

#include "sling/frame/serialization.h"
#include "sling/nlp/embedding/embedding-model.h"
#include "sling/nlp/kb/facts.h"
#include "sling/util/bloom.h"
#include "sling/util/embeddings.h"
#include "sling/util/random.h"
#include "sling/util/sortmap.h"
#endif

#include "sling/file/textmap.h"
#include "sling/frame/object.h"
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

// Fact plausibility model.
struct FactPlausibilityFlow : public Flow {
  // Build fact plausibility model.
  void Build(const Transformations &library) {
    // Build encoder.
    encoder = AddFunction("encoder");
    FlowBuilder e(this, encoder);
    auto *embeddings =
        e.Random(e.Parameter("embeddings", DT_FLOAT, {facts, dims}));
    pfeatures = e.Placeholder("pfeatures", DT_INT32, {1, max_features});
    hfeatures = e.Placeholder("hfeatures", DT_INT32, {1, max_features});
    auto *psum = e.GatherSum(embeddings, pfeatures);
    auto *hsum = e.GatherSum(embeddings, hfeatures);
    pencoding = e.Name(psum, "pencoding");
    hencoding = e.Name(hsum, "hencoding");

    // Build scorer.
    scorer = AddFunction("scorer");
    FlowBuilder s(this, scorer);
    premise = s.Placeholder("premise", DT_FLOAT, {1, dims});
    premise->set_ref()->set_unique();
    hypothesis = s.Placeholder("hypothesis", DT_FLOAT, {1, dims});
    hypothesis->set_ref()->set_unique();
    auto *fv = s.Concat({premise, hypothesis});
    logits = s.Name(s.FFLayers(fv, {dims * 2, 2}, -1, true, "Tanh"), "logits");

    // Create gradient computations.
    gencoder = Gradient(this, encoder, library);
    gscorer = Gradient(this, scorer, library);

    d_logits = GradientVar(logits);
    d_premise = GradientVar(premise);
    d_hypothesis = GradientVar(hypothesis);
    d_pencoding = GradientVar(pencoding);
    d_hencoding = GradientVar(hencoding);

    encoder_primal = PrimalVar(encoder);
    scorer_primal = PrimalVar(scorer);

    // Connect fact encodings to premise and hypothesis.
    Connect({pencoding, premise});
    Connect({hencoding, hypothesis});
    Connect({d_pencoding, d_premise});
    Connect({d_hencoding, d_hypothesis});
  }

  int facts = 1;                   // number of fact types
  int dims = 64;                   // dimension of embedding vectors
  int max_features = 512;          // maximum number of features per example

  // Fact encoder.
  Function *encoder;               // encoder function
  Function *gencoder;              // encoder gradient function
  Variable *embeddings;            // fact embedding matrix
  Variable *pfeatures;             // premise feature input
  Variable *hfeatures;             // hypothesis feature input
  Variable *pencoding;             // premise encoding
  Variable *hencoding;             // hypothesis encoding
  Variable *d_pencoding;           // premise gradient
  Variable *d_hencoding;           // hypothesis gradient
  Variable *encoder_primal;        // primal reference for encoder

  // Fact scorer.
  Function *scorer;                // plausibility scoring function
  Function *gscorer;               // plausibility scoring gradient function
  Variable *premise;               // premise encoding
  Variable *hypothesis;            // hypothesis encoding
  Variable *logits;                // plausibility prediction
  Variable *d_premise;             // premise encoding gradient
  Variable *d_hypothesis;          // hypothesis encoding gradient
  Variable *d_logits;              // plausibility gradient
  Variable *scorer_primal;         // primal reference for scorer
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
    while (factmap.Next()) fact_lexicon_.push_back(factmap.key());
    task->GetCounter("facts")->Increment(fact_lexicon_.size());

    // Build plausibility model.
    Build(&flow_);
    loss_.Build(&flow_, flow_.logits, flow_.d_logits);
    optimizer_ = GetOptimizer(task);
    optimizer_->Build(&flow_);

    // Compile plausibility model.
    myelin::Network model;
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
    myelin::LogProfile(model);

#if 0
    // Write fact embeddings to output file.
    LOG(INFO) << "Writing embeddings";
    myelin::TensorData W0 = model[flow_.left.embeddings];
    std::vector<float> embedding(embedding_dims_);
    EmbeddingWriter fact_writer(task->GetOutputFile("factvecs"),
                                fact_lexicon.size(), embedding_dims_);
    for (int i = 0; i < fact_lexicon.size(); ++i) {
      for (int j = 0; j < embedding_dims_; ++j) {
        embedding[j] = W0.at<float>(i, j);
      }
      fact_writer.Write(fact_lexicon[i], embedding);
    }
    CHECK(fact_writer.Close());

    // Write category embeddings to output file.
    myelin::TensorData W1 =  model[flow_.right.embeddings];
    EmbeddingWriter category_writer(task->GetOutputFile("catvecs"),
                                    category_lexicon.size(), embedding_dims_);
    for (int i = 0; i < category_lexicon.size(); ++i) {
      for (int j = 0; j < embedding_dims_; ++j) {
        embedding[j] = W1.at<float>(i, j);
      }
      category_writer.Write(category_lexicon[i], embedding);
    }
    CHECK(category_writer.Close());

#endif

    delete optimizer_;
  }

  // Add plausibility model to flow.
  void Build(FactPlausibilityFlow *flow) {
    flow->facts = fact_lexicon_.size();
    flow->dims = embedding_dims_;
    flow->max_features = max_features_;
    flow->Build(*compiler_.library());
  }

  // Worker thread for training embedding model.
  void Worker(int index, myelin::Network *model) override {
    // Initialize batch.
    Random rnd;
    rnd.seed(index);

    std::vector<Instance *> samples;
    Cell *encoder = model->GetCell(flow_.encoder);
    Tensor *pfeatures = encoder->GetParameter(flow_.pfeatures);
    Tensor *hfeatures = encoder->GetParameter(flow_.hfeatures);
    Tensor *pencoding = encoder->GetParameter(flow_.pencoding);
    Tensor *hencoding = encoder->GetParameter(flow_.hencoding);
    for (int i = 0; i < batch_size_; ++i) {
      samples.push_back(new Instance(encoder));
    }

    Instance scorer(model->GetCell(flow_.scorer));
    Tensor *premise = scorer.cell()->GetParameter(flow_.premise);
    Tensor *hypothesis = scorer.cell()->GetParameter(flow_.hypothesis);

    Instance gscorer(model->GetCell(flow_.gscorer));
    //Tensor *dpremise = gscorer.cell()->GetParameter(flow_.d_premise);
    //Tensor *dhypothesis = gscorer.cell()->GetParameter(flow_.d_hypothesis);

    float *logits = scorer.Get<float>(flow_.logits);
    float *dlogits = gscorer.Get<float>(flow_.d_logits);

    for (;;) {
      // Compute gradients for epoch.
      gscorer.Clear();
      gscorer.Set(flow_.scorer_primal, &scorer);
      float epoch_loss = 0.0;

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
          int *p = samples[i]->Get<int>(pfeatures);
          int *pend = p + flow_.max_features;
          int *h = samples[i]->Get<int>(hfeatures);
          int *hend = h + flow_.max_features;
          int heldout = rnd.UniformInt(num_groups);
          for (int g = 0; g < num_groups; ++g) {
            // Get range for fact group.
            int start = g == 0 ? 0 : groups.get(g - 1).AsInt();
            int end = groups.get(g).AsInt();

            if (g == heldout) {
              // Add fact group to hyothesis.
              for (int f = start; f < end; ++f) {
                if (h == hend) {
                  num_feature_overflows_->Increment();
                  break;
                }
                *h++ = facts.get(f).AsInt();
              }
            } else {
              // Add fact group to premise.
              for (int f = start; f < end; ++f) {
                if (p == pend) {
                  num_feature_overflows_->Increment();
                  break;
                }
                *p++ = facts.get(f).AsInt();
              }
            }
          }
          if (p < pend) *p = -1;
          if (h < hend) *h = -1;

          // Compute encodings for premise and hypothesis.
          samples[b]->Compute();
        }

        // Do forward and backward propagation for each premise/hypothesis pair.
        // Each sampled item is a positive example. Negative examples are
        // generated by using the premise from one item and the hypothsis from
        // another item.
        for (int i = 0; i < batch_size_; ++i) {
          for (int j = 0; j < batch_size_; ++j) {
            // Set premise and hypothesis encodings for example.
            float *p = samples[i]->Get<float>(pencoding);
            float *h = samples[j]->Get<float>(hencoding);
            scorer.SetReference(premise, p);
            scorer.SetReference(hypothesis, h);

            // Compute logits.
            scorer.Compute();

            // Compute loss.
            int label = i == j ? 1 : 0;
            float loss = loss_.Compute(logits, label, dlogits);
            epoch_loss += loss;

            // Propagate loss back through scorer.


          }
        }

#if 0
        // Process batch.
        float loss = batch.Compute();
#endif
      }

#if 0
      // Update parameters.
      optimizer_mu_.Lock();
      optimizer_->Apply(batch.gradients());
      loss_sum_ += epoch_loss;
      loss_count_ += batches_per_update_;
      optimizer_mu_.Unlock();
#endif

      // Check if we are done.
      if (EpochCompleted()) break;
    }

    for (int i = 0; i < batch_size_; ++i) {
      delete samples[i];
    }
  }

  // Evaluate model.
  bool Evaluate(int64 epoch, myelin::Network *model) override {
#if 0
    // Skip evaluation if there are no data.
    if (loss_count_ == 0) return true;

    // Compute average loss of epochs since last eval.
    float loss = loss_sum_ / loss_count_;
    float p = exp(-loss) * 100.0;
    loss_sum_ = 0.0;
    loss_count_ = 0;

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
              << ", p=" << p;
#endif

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
  std::vector<string> fact_lexicon_;

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

#if 0
  // Mutex for serializing access to optimizer.
  Mutex optimizer_mu_;

  float prev_loss_ = 0.0;
  float loss_sum_ = 0.0;
  int loss_count_ = 0.0;
#endif

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


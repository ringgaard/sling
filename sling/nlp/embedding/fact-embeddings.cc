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

#include <atomic>
#include <utility>

#include "sling/file/textmap.h"
#include "sling/frame/object.h"
#include "sling/frame/store.h"
#include "sling/frame/serialization.h"
#include "sling/myelin/builder.h"
#include "sling/myelin/compiler.h"
#include "sling/myelin/learning.h"
#include "sling/nlp/embedding/embedding-model.h"
#include "sling/nlp/kb/facts.h"
#include "sling/task/frames.h"
#include "sling/task/process.h"
#include "sling/util/bloom.h"
#include "sling/util/embeddings.h"
#include "sling/util/mutex.h"
#include "sling/util/random.h"
#include "sling/util/sortmap.h"

namespace sling {
namespace nlp {

using namespace task;

// Extract fact and category lexicons from items.
class FactLexiconExtractor : public Process {
 public:
  void Run(Task *task) override {
    // Get parameters.
    int64 bloom_size = task->Get("bloom_size", 4000000000L);
    int bloom_hashes = task->Get("bloom_hashes", 4);
    int fact_threshold = task->Get("fact_threshold", 10);
    int category_threshold = task->Get("category_threshold", 10);

    // Set up counters.
    Counter *num_items = task->GetCounter("items");
    Counter *num_facts = task->GetCounter("facts");
    Counter *num_fact_types = task->GetCounter("fact_types");
    Counter *num_filtered = task->GetCounter("filtered_facts");
    Counter *num_facts_selected = task->GetCounter("facts_selected");
    Counter *num_categories_selected = task->GetCounter("categories_selected");

    // Load knowledge base.
    Store commons;
    LoadStore(task->GetInputFile("kb"), &commons);

    // Resolve symbols.
    Names names;
    Name p_item_category(names, "/w/item/category");
    Name n_item(names, "/w/item");
    Name p_instance_of(names, "P31");
    Name n_wikimedia_category(names, "Q4167836");
    Name n_wikimedia_disambiguation(names, "Q4167410");

    names.Bind(&commons);

    // Initialize fact catalog.
    FactCatalog catalog;
    catalog.Init(&commons);
    commons.Freeze();

    // A Bloom filter is used for checking for singleton facts. It is used as
    // a fast and compact check for detecting if a fact is a new fact. The
    // probabilistic nature of the Bloom filter means that the fact instance
    // counts can be off by one.
    BloomFilter filter(bloom_size, bloom_hashes);

    // The categories are collected in a sortable hash map so the most frequent
    // categories can be selected.
    SortableMap<Handle, int64, HandleHash> category_lexicon;

    // The facts are collected in a sortable hash map where the key is the
    // fact fingerprint. The name of the fact is stored in a nul-terminated
    // dynaminally allocated string.
    SortableMap<int64, std::pair<int64, char *>> fact_lexicon;

    // Extract facts from all items in the knowledge base.
    commons.ForAll([&](Handle handle) {
      Frame item(&commons, handle);
      if (!item.IsA(n_item)) return;

      // Skip categories and disambiguation page items.
      Handle cls = item.GetHandle(p_instance_of);
      if (cls == n_wikimedia_category) return;
      if (cls == n_wikimedia_disambiguation) return;

      // Extract facts from item.
      Store store(&commons);
      Facts facts(&catalog, &store);
      facts.Extract(handle);

      // Add facts to fact lexicon.
      for (Handle fact : facts.list()) {
        int64 fp = store.Fingerprint(fact);
        if (filter.add(fp)) {
          auto &entry = fact_lexicon[fp];
          if (entry.second == nullptr) {
            entry.second = strdup(ToText(&store, fact).c_str());
            num_fact_types->Increment();
          }
          entry.first++;
        } else {
          num_filtered->Increment();
        }
      }
      num_facts->Increment(facts.list().size());

      // Extract categories from item.
      for (const Slot &s : item) {
        if (s.name == p_item_category) {
          category_lexicon[s.value]++;
        }
      }

      num_items->Increment();
    });
    task->GetCounter("num_categories")->Increment(category_lexicon.map.size());

    // Write fact lexicon to text map.
    fact_lexicon.sort();
    TextMapOutput factout(task->GetOutputFile("factmap"));
    for (int i = fact_lexicon.array.size() - 1; i >= 0; --i) {
      Text fact(fact_lexicon.array[i]->second.second);
      int64 count = fact_lexicon.array[i]->second.first;
      if (count < fact_threshold) break;
      factout.Write(fact, count);
      num_facts_selected->Increment();
    }
    factout.Close();

    // Write category lexicon to text map.
    category_lexicon.sort();
    TextMapOutput catout(task->GetOutputFile("catmap"));
    for (int i = category_lexicon.array.size() - 1; i >= 0; --i) {
      Frame cat(&commons, category_lexicon.array[i]->first);
      int64 count = category_lexicon.array[i]->second;
      if (count < category_threshold) break;
      catout.Write(cat.Id(), count);
      num_categories_selected->Increment();
    }
    catout.Close();

    // Clean up.
    for (auto &it : fact_lexicon.map) free(it.second.second);
  }
};

REGISTER_TASK_PROCESSOR("fact-lexicon-extractor", FactLexiconExtractor);

// Extract facts items.
class FactExtractor : public Process {
 public:
  void Run(Task *task) override {
    // Set up counters.
    Counter *num_items = task->GetCounter("items");
    Counter *num_facts = task->GetCounter("facts");
    Counter *num_facts_extracted = task->GetCounter("facts_extracted");
    Counter *num_facts_skipped = task->GetCounter("facts_skipped");
    Counter *num_no_facts = task->GetCounter("items_without_facts");
    Counter *num_cats = task->GetCounter("categories");
    Counter *num_cats_extracted = task->GetCounter("categories_extracted");
    Counter *num_cats_skipped = task->GetCounter("categories_skipped");
    Counter *num_no_cats = task->GetCounter("items_without_categories");

    // Load knowledge base.
    LoadStore(task->GetInputFile("kb"), &commons_);

    // Resolve symbols.
    names_.Bind(&commons_);

    // Initialize fact catalog.
    FactCatalog catalog;
    catalog.Init(&commons_);
    commons_.Freeze();

    // Read fact and category lexicons.
    ReadFactLexicon(task->GetInputFile("factmap"));
    ReadCategoryLexicon(task->GetInputFile("catmap"));

    // Get output channel for resolved fact frames.
    Channel *output = task->GetSink("output");

    // Extract facts from all items in the knowledge base.
    commons_.ForAll([&](Handle handle) {
      Frame item(&commons_, handle);
      if (!item.IsA(n_item_)) return;

      // Skip categories and disambiguation page items.
      Handle cls = item.GetHandle(p_instance_of_);
      if (cls == n_wikimedia_category_) return;
      if (cls == n_wikimedia_disambiguation_) return;

      // Extract facts from item.
      Store store(&commons_);
      Facts facts(&catalog, &store);
      facts.Extract(handle);

      // Add facts to fact lexicon.
      Handles fact_indicies(&store);
      for (Handle fact : facts.list()) {
        int64 fp = store.Fingerprint(fact);
        auto f = fact_lexicon_.find(fp);
        if (f != fact_lexicon_.end()) {
          fact_indicies.push_back(Handle::Integer(f->second));
        }
      }
      int total = facts.list().size();
      int extracted = fact_indicies.size();
      int skipped = total - extracted;
      num_facts->Increment(total);
      num_facts_extracted->Increment(extracted);
      num_facts_skipped->Increment(skipped);
      if (extracted == 0) num_no_facts->Increment();

      // Extract categories from item.
      Handles category_indicies(&store);
      for (const Slot &s : item) {
        if (s.name == p_item_category_) {
          auto f = category_lexicon_.find(s.value);
          if (f != category_lexicon_.end()) {
            category_indicies.push_back(Handle::Integer(f->second));
            num_cats_extracted->Increment();
          } else {
            num_cats_skipped->Increment();
          }
          num_cats->Increment();
        }
      }
      if (category_indicies.empty()) num_no_cats->Increment();

      // Build frame with resolved facts.
      Builder builder(&store);
      builder.Add(p_item_, item.id());
      builder.Add(p_facts_, Array(&store, fact_indicies));
      builder.Add(p_categories_, Array(&store, category_indicies));

      // Output frame with resolved facts on output channel.
      output->Send(CreateMessage(item.Id(), builder.Create()));
      num_items->Increment();
    });
  }

 private:
  // Read fact lexicon.
  void ReadFactLexicon(const string &filename) {
    Store store(&commons_);
    TextMapInput factmap(filename);
    string key;
    int index;
    while (factmap.Read(&index, &key, nullptr)) {
      uint64 fp = FromText(&store, key).Fingerprint();
      fact_lexicon_[fp] = index;
    }
  }

  // Read category lexicon.
  void ReadCategoryLexicon(const string &filename) {
    TextMapInput catmap(filename);
    string key;
    int index;
    while (catmap.Read(&index, &key, nullptr)) {
      Handle cat = commons_.Lookup(key);
      category_lexicon_[cat] = index;
    }
  }

  // Commons store with knowledge base.
  Store commons_;

  // Fact lexicon mapping from fact fingerprint to fact index.
  std::unordered_map<uint64, int> fact_lexicon_;

  // Category lexicon mapping from category handle to category index.
  HandleMap<int> category_lexicon_;

  // Symbols.
  Names names_;
  Name p_item_category_{names_, "/w/item/category"};
  Name n_item_{names_, "/w/item"};
  Name p_instance_of_{names_, "P31"};
  Name n_wikimedia_category_{names_, "Q4167836"};
  Name n_wikimedia_disambiguation_{names_, "Q4167410"};

  Name p_item_{names_, "item"};
  Name p_facts_{names_, "facts"};
  Name p_categories_{names_, "categories"};
};

REGISTER_TASK_PROCESSOR("fact-extractor", FactExtractor);

// Trainer for fact embeddings model.
class FactEmbeddingsTrainer : public Process {
 public:
  // Run training of embedding net.
  void Run(Task *task) override {
    // Get training parameters.
    task->Fetch("epochs", &epochs_);
    task->Fetch("embedding_dims", &embedding_dims_);
    task->Fetch("batch_size", &batch_size_);
    task->Fetch("max_features", &max_features_);
    task->Fetch("threads", &threads_);
    task->Fetch("learning_rate", &learning_rate_);
    task->Fetch("learning_rate_decay", &learning_rate_decay_);
    task->Fetch("min_learning_rate", &min_learning_rate_);
    task->Fetch("eval_interval", &eval_interval_);

    // Set up counters.
    Counter *num_instances = task->GetCounter("instances");
    Counter *num_instances_skipped = task->GetCounter("instances_skipped");
    num_epochs_completed_ = task->GetCounter("epochs_completed");
    num_feature_overflows_ = task->GetCounter("feature_overflows");

    // Bind names.
    names_.Bind(&store_);

    // Read fact lexicon.
    std::vector<string> fact_lexicon;
    TextMapInput factmap(task->GetInputFile("factmap"));
    while (factmap.Next()) fact_lexicon.push_back(factmap.key());
    int fact_dims = fact_lexicon.size();
    task->GetCounter("facts")->Increment(fact_dims);

    // Read category lexicon.
    std::vector<string> category_lexicon;
    TextMapInput catmap(task->GetInputFile("catmap"));
    while (catmap.Next()) {
      category_lexicon.push_back(catmap.key());
    }
    int category_dims = category_lexicon.size();
    task->GetCounter("categories")->Increment(category_dims);

    // Build dual encoder model with facts on the left side and categories on
    // the right side.
    LOG(INFO) << "Building model";
    myelin::Compiler compiler;
    flow_.dims = embedding_dims_;
    flow_.batch_size = batch_size_;
    flow_.left.dims = fact_dims;
    flow_.left.max_features = max_features_;
    flow_.right.dims = category_dims;
    flow_.left.max_features = max_features_;
    flow_.Build(*compiler.library());
    loss_.Build(&flow_, flow_.similarities, flow_.gsimilarities);

    // Compile embedding model.
    LOG(INFO) << "Compiling model";
    myelin::Network model;
    compiler.Compile(&flow_, &model);
    loss_.Initialize(model);

    // Initialize weights.
    LOG(INFO) << "Initializing model";
    model.InitLearnableWeights(0, 0.0, 1e-4);

    // Read training instances from input.
    LOG(INFO) << "Reading facts";
    Queue input(this, task->GetSources("input"));
    Message *message;
    while (input.Read(&message)) {
      // Parse message into frame.
      Frame instance = DecodeMessage(&store_, message);
      Array facts = instance.Get(p_facts_).AsArray();
      Array categories = instance.Get(p_categories_).AsArray();
      if (facts.length() > 0 && categories.length() > 0) {
        instances_.push_back(instance.handle());
        num_instances->Increment();
      } else {
        num_instances_skipped->Increment();
      }

      delete message;
    }
    store_.Freeze();
    task->GetCounter("epochs_total")->Increment(epochs_);

    // Start training threads.
    LOG(INFO) << "Training model";
    WorkerPool pool;
    pool.Start(threads_, [this, &model](int index) { Worker(index, &model); });

    // Evaluate model on regular intervals. The workers signal when it is time
    // for the next eval round.
    for (;;) {
      // Wait for next eval.
      {
        std::unique_lock<std::mutex> lock(eval_mu_);
        eval_model_.wait(lock);
      }

      // Evaluate model.
      int64 epoch = epochs_completed_;
      float epochs = epoch - eval_epoch_;
      float pos_loss = pos_loss_sum_ / epochs;
      float neg_loss = neg_loss_sum_ / epochs;
      float loss = pos_loss + neg_loss;
      eval_epoch_ = epoch;
      pos_loss_sum_ = 0.0;
      neg_loss_sum_ = 0.0;

      // Decay learning rate if loss increases.
      if (prev_loss_ != 0.0 && prev_loss_ < loss) {
        double lr = learning_rate_ * learning_rate_decay_;
        if (lr < min_learning_rate_) lr = min_learning_rate_;
        learning_rate_ = lr;
      }
      prev_loss_ = loss;

      LOG(INFO) << "epoch=" << epoch << ", "
                << "lr=" << learning_rate_ << ", "
                << "+loss=" << pos_loss  << ", "
                << "-loss=" << neg_loss;

      // Check if we are done.
      if (epoch >= epochs_) break;
    }

    // Wait until workers completes.
    pool.Join();

    // Output profile.
    myelin::LogProfile(&model);

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
  }

  // Worker thread for training embedding model.
  void Worker(int index, myelin::Network *model) {
#if 0
    Random rnd;
    rnd.seed(index);

    // Set up model compute instances.
    myelin::Instance l0(model->GetCell(flow_.layer0));
    myelin::Instance l1(model->GetCell(flow_.layer1));
    myelin::Instance l0b(model->GetCell(flow_.layer0b));

    int *features = l0.Get<int>(model->GetParameter(flow_.fv));
    int *fend = features + max_features_;
    int *target = l1.Get<int>(model->GetParameter(flow_.target));
    int *tend = target + max_features_;
    float *label = l1.Get<float>(model->GetParameter(flow_.label));
    float *alpha = l1.Get<float>(model->GetParameter(flow_.alpha));
    float *loss = l1.Get<float>(model->GetParameter(flow_.loss));
    myelin::Tensor *error = model->GetParameter(flow_.error);
    //int category_dims = flow_.W1->dim(0);

    l1.Set(model->GetParameter(flow_.l1_l0), &l0);
    l0b.Set(model->GetParameter(flow_.l0b_l0), &l0);
    l0b.Set(model->GetParameter(flow_.l0b_l1), &l1);

    // Random sample instances from training data.
    for (;;) {
      // Get next instance.
      int sample = rnd.UniformInt(instances_.size());
      Frame instance(&store_, instances_[sample]);
      Array facts = instance.Get(p_facts_).AsArray();
      Array categories = instance.Get(p_categories_).AsArray();

      // Initialize features with facts.
      int *f = features;
      for (int i = 0; i < facts.length(); ++i) {
        if (f == fend) {
          num_feature_overflows_->Increment();
          break;
        }
        *f++ = facts.get(i).AsInt();
      }
      if (f < fend) *f = -1;

      // Propagate input to hidden layer.
      l0.Compute();

      // Propagate hidden to output and back. This also accumulates the
      // errors that should be propagated back to the input layer.
      l1.Clear(error);
      *label = 1.0;
      int *pos = target;
      for (int i = 0; i < categories.length(); ++i) {
        if (pos == tend) {
          num_feature_overflows_->Increment();
          break;
        }
        *pos++ = categories.get(i).AsInt();
      }
      if (pos < tend) *pos = -1;
      l1.Compute();
      pos_loss_sum_ += *loss;

      // Sample negative example from random instances.
      *label = 0.0;
      int *neg = target;
      for (int d = 0; d < negative_; ++d) {
        int negid = rnd.UniformInt(instances_.size());
        Frame negative(&store_, instances_[negid]);
        Array negcat = negative.Get(p_categories_).AsArray();
        for (int i = 0; i < negcat.length(); ++i) {
          if (neg == tend) {
            num_feature_overflows_->Increment();
            break;
          }
        *neg++ = negcat.get(i).AsInt();
        }
      }
      if (neg < tend) *neg = -1;
      l1.Compute();
      neg_loss_sum_ -= *loss;

      // Propagate hidden to input.
      l0b.Compute();

      // Check if we are done.
      num_epochs_completed_->Increment();
      int64 epoch = ++epochs_completed_;
      if (epoch >= epochs_) {
        SignalEval();
        break;
      } else if (epoch % eval_interval_ == 0) {
        SignalEval();
      }

      // Update learning rate.
      *alpha = learning_rate_;
    }
#else
    SignalEval();
#endif
  }

  // Signal next evaluation round.
  void SignalEval() {
    std::unique_lock<std::mutex> lock(eval_mu_);
    eval_model_.notify_one();
  }

 private:
  // Flow model for fact embedding trainer.
  DualEncoderFlow flow_;
  myelin::CrossEntropyLoss loss_;

  // Store for training instances.
  Store store_;

  // Training instances.
  Handles instances_{&store_};

  // Training parameters.
  int epochs_ = 1000;                  // number of training epochs
  int embedding_dims_ = 256;           // size of embedding vectors
  int max_features_ = 512;             // maximum features per item
  int threads_ = 5;                    // number of training worker threads
  double learning_rate_ = 0.025;       // learning rate
  double learning_rate_decay_ = 0.5;   // learning rate decay rate
  double min_learning_rate_ = 0.0001;  // minimum learning rate
  int batch_size_ = 1024;              // number of examples per epoch
  int eval_interval_ = 10000000;       // epochs between evaluations

  // Current number of completed epochs.
  std::atomic<int64> epochs_completed_{0};

  // Signal model evaluation.
  Mutex eval_mu_;
  std::condition_variable eval_model_;

  // Evaluation statistics.
  int64 eval_epoch_ = 0;
  float prev_loss_ = 0.0;
  float pos_loss_sum_ = 0.0;
  float neg_loss_sum_ = 0.0;

  // Symbols.
  Names names_;
  Name p_item_{names_, "item"};
  Name p_facts_{names_, "facts"};
  Name p_categories_{names_, "categories"};

  // Staticstics.
  Counter *num_epochs_completed_ = nullptr;
  Counter *num_feature_overflows_ = nullptr;
};

REGISTER_TASK_PROCESSOR("fact-embeddings-trainer", FactEmbeddingsTrainer);

}  // namespace nlp
}  // namespace sling

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

#include <math.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "sling/file/textmap.h"
#include "sling/frame/serialization.h"
#include "sling/frame/store.h"
#include "sling/myelin/flow.h"
#include "sling/myelin/compiler.h"
#include "sling/myelin/compute.h"
#include "sling/nlp/document/annotator.h"
#include "sling/nlp/document/document.h"
#include "sling/nlp/document/document-corpus.h"
#include "sling/nlp/parser/parser-codec.h"
#include "sling/nlp/parser/frame-evaluation.h"
#include "sling/task/learner.h"
#include "sling/util/mutex.h"

namespace sling {
namespace nlp {

using namespace task;
using namespace myelin;

// Task for training frame-semantic parsers.
class ParserTrainer : public task::LearnerTask {
 public:
  ~ParserTrainer() {
    delete encoder_;
    delete decoder_;
    delete training_corpus_;
    delete evaluation_corpus_;
  }

  // Run parser training.
  void Run(task::Task *task) override {
    // Get training parameters.
    task->Fetch("batch_size", &batch_size_);
    task->Fetch("learning_rate", &learning_rate_);
    task->Fetch("min_learning_rate", &min_learning_rate_);
    task->Fetch("learning_rate_cliff", &learning_rate_cliff_);
    task->Fetch("skip_mask", &skip_mask_);

    // Save task parameters.
    for (auto &p : task->parameters()) {
      hparams_.emplace_back(p.name, p.value);
    }

    // Statistics.
    num_tokens_ = task->GetCounter("tokens");
    num_documents_ = task->GetCounter("documents");

    // Load commons store from file.
    for (Binding *binding : task->GetInputs("commons")) {
      LoadStore(binding->filename(), &commons_);
    }

    // Initialize preprocessing pipeline.
    pipeline_.Init(task, &commons_);

    // Open training and evaluation corpora.
    auto train =  task->GetInputFiles("training_corpus");
    auto eval =  task->GetInputFiles("evaluation_corpus");
    training_corpus_ = new DocumentCorpus(&commons_, train);
    if (!eval.empty()) {
      evaluation_corpus_ = new DocumentCorpus(&commons_, eval);
    }

    // Output file for model.
    auto *model_file = task->GetOutput("model");
    if (model_file != nullptr) {
      model_filename_ = model_file->resource()->name();
    }

    // Initialize word vocabulary.
    auto *vocabulary = task->GetInput("vocabulary");
    if (vocabulary != nullptr) {
      // Read vocabulary from text map file.
      TextMapInput input(vocabulary->filename());
      string word;
      int64 count;
      while (input.Read(nullptr, &word, &count)) words_[word] += count;
    } else {
      // Initialize word vocabulary from training data.
      training_corpus_->Rewind();
      for (;;) {
        Document *document = training_corpus_->Next(&commons_);
        if (document == nullptr) break;
        pipeline_.Annotate(document);
        for (const Token &t : document->tokens()) words_[t.word()]++;
        delete document;
      }
    }

    // Set up encoder.
    encoder_ = ParserEncoder::Create(task->Get("encoder", "lexrnn"));
    encoder_->Setup(task, &commons_);

    // Set up decoder.
    decoder_ = ParserDecoder::Create(task->Get("decoder", "transition"));
    decoder_->Setup(task, &commons_);

    // Build parser model flow graph.
    Flow flow;
    Build(&flow, true);
    optimizer_ = GetOptimizer(task);
    optimizer_->Build(&flow);

    // Compile model.
    compiler_.Compile(&flow, &model_);

    // Initialize model.
    model_.InitModelParameters(task->Get("seed", 0));
    encoder_->Initialize(model_);
    decoder_->Initialize(model_);
    optimizer_->Initialize(model_);
    commons_.Freeze();

    // Optionally load initial model parameters for restart.
    auto *initial_model = task->GetInput("initial_model");
    if (initial_model != nullptr) {
      LOG(INFO) << "Load model parameters from " << initial_model->filename();
      Flow initial;
      CHECK(initial.Load(initial_model->filename()));
      model_.LoadParameters(initial);
    }

    // Train model.
    Train(task, &model_);

    // Save final model.
    if (!model_filename_.empty()) {
      LOG(INFO) << "Writing parser model to " << model_filename_;
      Save(model_filename_);
    }

    // Clean up.
    delete optimizer_;
  }

  // Training worker thread.
  void Worker(int index, Network *model) override {
    // Create encoder and decoder instances.
    ParserEncoder::Learner *encoder = encoder_->CreateLearner();
    ParserDecoder::Learner *decoder = decoder_->CreateLearner();

    // Collect gradients.
    Instances gradients;
    encoder->CollectGradients(&gradients);
    decoder->CollectGradients(&gradients);

    // Training loop.
    for (;;) {
      // Prepare next batch.
      gradients.Clear();
      encoder->NextBatch();
      decoder->NextBatch();

      for (int b = 0; b < batch_size_; b++) {
        // Get next training document.
        Store store(&commons_);
        Document *document = GetNextTrainingDocument(&store);
        pipeline_.Annotate(document);
        num_documents_->Increment();
        num_tokens_->Increment(document->length());

        decoder->Switch(document);
        for (SentenceIterator s(document, skip_mask_); s.more(); s.next()) {
          // Run sentence through encoder to produce contextual token encodings.
          auto *encodings = encoder->Encode(*document, s.begin(), s.end());

          // Run decoder on token encodings to learn the annotations.
          auto *dencodings = decoder->Learn(s.begin(), s.end(), encodings);

          // Propagate gradients back through encoder.
          encoder->Backpropagate(dencodings);
        }

        delete document;
      }

      // Update parameters.
      update_mu_.Lock();
      optimizer_->Apply(gradients);
      decoder->UpdateLoss(&loss_sum_, &loss_count_);
      update_mu_.Unlock();

      // Check if we are done.
      if (EpochCompleted()) break;
    }

    // Clean up.
    delete encoder;
    delete decoder;
  }

  // Evaluate current parser model.
  bool Evaluate(int64 epoch, Network *model) override {
    // Skip evaluation if there are no data.
    if (loss_count_ == 0) return true;

    // Compute average loss of epochs since last eval.
    float loss = loss_sum_ / loss_count_;
    float p = exp(-loss) * 100.0;
    loss_sum_ = 0.0;
    loss_count_ = 0;

    // Decay learning rate if loss increases.
    bool decay = prev_loss_ != 0.0 && prev_loss_ < loss;
    if (learning_rate_cliff_ != 0) {
      if (epoch >= learning_rate_cliff_) decay = true;
    }
    if (learning_rate_ <= min_learning_rate_) decay = false;
    if (decay) learning_rate_ = optimizer_->DecayLearningRate();
    prev_loss_ = loss;

    LOG(INFO) << "epoch=" << epoch
              << " lr=" << learning_rate_
              << " loss=" << loss
              << " p=" << p;

    // Evaluate current model on held-out evaluation corpus.
    if (evaluation_corpus_ != nullptr) {
      ParserEvaulationCorpus corpus(this);
      FrameEvaluation::Output eval;
      FrameEvaluation::Evaluate(&corpus, &eval);
      FrameEvaluation::Benchmarks benchmarks;
      eval.GetBenchmarks(&benchmarks);
      for (const auto &benchmark : benchmarks) {
        LOG(INFO) << benchmark.Summary();
      }
    }

    return true;
  }

  // Checkpoint current model to the output model file.
  void Checkpoint(int64 epoch, Network *model) override {
    if (!model_filename_.empty()) {
      LOG(INFO) << "Checkpointing model to " << model_filename_;
      Save(model_filename_);
    }
  }

 private:
  // Parallel corpus for evaluating parser on golden corpus.
  class ParserEvaulationCorpus : public ParallelCorpus {
   public:
    ParserEvaulationCorpus(ParserTrainer *trainer) : trainer_(trainer) {
      trainer_->evaluation_corpus_->Rewind();
    }

    // Parse next evaluation document using parser model.
    bool Next(Store **store, Document **golden, Document **predicted) override {
      // Create a store for both golden and parsed document.
      Store *local = new Store(&trainer_->commons_);

      // Read next document from corpus.
      Document *document = trainer_->evaluation_corpus_->Next(local);
      if (document == nullptr) {
        delete local;
        return false;
      }

      // Preprocess document.
      trainer_->pipeline_.Annotate(document);

      // Clone document without annotations.
      Document *parsed = new Document(*document, false);

      // Parse the document using the current model.
      trainer_->Parse(parsed);
      parsed->Update();

      // Return golden and predicted documents.
      *store = local;
      *golden = document;
      *predicted = parsed;

      return true;
    }

    // Return commons store for corpus.
    Store *Commons() override { return &trainer_->commons_; }

   private:
    ParserTrainer *trainer_;   // parser trainer with current model
  };

  // Build flow graph for parser model.
  void Build(Flow *flow, bool learn) {
    // Build encoder.
    Flow::Variable *encodings;
    if (learn) {
      Vocabulary::HashMapIterator vocab(words_);
      encodings = encoder_->Build(flow, &vocab, true);
    } else {
      encodings = encoder_->Build(flow, nullptr, false);
    }

    // Build decoder.
    decoder_->Build(flow, encodings, learn);
  }

  // Read next training document into store. The caller owns the returned
  // document.
  Document *GetNextTrainingDocument(Store *store) {
    MutexLock lock(&input_mu_);
    Document *document = training_corpus_->Next(store);
    if (document == nullptr) {
      // Loop around if the end of the training corpus has been reached.
      training_corpus_->Rewind();
      document = training_corpus_->Next(store);
    }
    return document;
  }

  // Parse document using current model.
  void Parse(Document *document) const {
    // Create encoder and decoder predictors.
    ParserEncoder::Predictor *encoder = encoder_->CreatePredictor();
    ParserDecoder::Predictor *decoder = decoder_->CreatePredictor();

    // Parse each sentence of the document.
    decoder->Switch(document);
    for (SentenceIterator s(document, skip_mask_); s.more(); s.next()) {
      // Encode tokens in the sentence using encoder.
      auto *encodings = encoder->Encode(*document, s.begin(), s.end());

      // Decode sentence using decoder.
      decoder->Decode(s.begin(), s.end(), encodings);
    }

    delete encoder;
    delete decoder;
  }

  // Save trained model to file.
  void Save(const string &filename) {
    // Build model.
    Flow flow;
    Build(&flow, false);

    // Copy weights from trained model.
    model_.SaveParameters(&flow);

    // Make parser specification frame.
    Store store(&commons_);
    Builder spec(&store);
    spec.Set("skip_mask", skip_mask_);

    // Save encoder.
    Builder encoder_spec(&store);
    encoder_->Save(&flow, &encoder_spec);
    spec.Set("encoder", encoder_spec.Create());

    // Save decoder.
    Builder decoder_spec(&store);
    decoder_->Save(&flow, &decoder_spec);
    spec.Set("decoder", decoder_spec.Create());

    // Save hyperparameters in flow.
    Builder params(&store);
    for (auto &p : hparams_) {
      params.Add(String(&store, p.first), p.second);
    }
    spec.Set("hparams", params.Create());

    // Save parser spec in flow.
    StringEncoder encoder(&store);
    encoder.Encode(spec.Create());

    Flow::Blob *blob = flow.AddBlob("parser", "frame");
    blob->data = flow.AllocateMemory(encoder.buffer());
    blob->size = encoder.buffer().size();

    // Save model to file.
    DCHECK(flow.IsConsistent());
    flow.Save(filename);
  }

  // Commons store for parser.
  Store commons_;

  // Pipeline for preprocessing training and evaluation documents.
  Pipeline pipeline_;

  // Training corpus.
  DocumentCorpus *training_corpus_ = nullptr;

  // Evaluation corpus.
  DocumentCorpus *evaluation_corpus_ = nullptr;

  // File name for trained model.
  string model_filename_;

  // Word vocabulary.
  std::unordered_map<string, int> words_;

  // Sentence skip mask. Default to skipping headings.
  int skip_mask_ = HEADING_BEGIN;

  // Neural network.
  Network model_;
  Compiler compiler_;
  Optimizer *optimizer_ = nullptr;

  // Parser encoder and decoder.
  ParserEncoder *encoder_ = nullptr;
  ParserDecoder *decoder_ = nullptr;

  // Mutexes for serializing access to global state.
  Mutex input_mu_;
  Mutex update_mu_;

  // Model hyperparameters.
  int batch_size_ = 32;
  int learning_rate_cliff_ = 0;
  float learning_rate_ = 1.0;
  float min_learning_rate_ = 0.001;

  // Evaluation statistics.
  float prev_loss_ = 0.0;
  float loss_sum_ = 0.0;
  int loss_count_ = 0.0;

  // Training task parameters.
  std::vector<std::pair<string, string>> hparams_;

  // Statistics.
  task::Counter *num_documents_;
  task::Counter *num_tokens_;
};

REGISTER_TASK_PROCESSOR("parser-trainer", ParserTrainer);

}  // namespace nlp
}  // namespace sling


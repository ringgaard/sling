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

#include "sling/nlp/parser/tagger-trainer.h"

#include "sling/frame/serialization.h"
#include "sling/myelin/gradient.h"
#include "sling/nlp/document/document.h"
#include "sling/nlp/document/lexicon.h"
#include "sling/util/unicode.h"

namespace sling {
namespace nlp {

using namespace task;
using namespace myelin;

TaggerTrainer::~TaggerTrainer() {
  delete training_corpus_;
  delete evaluation_corpus_;
}

void TaggerTrainer::Run(task::Task *task) {
  // Get training parameters.
  task->Fetch("rnn_dim", &rnn_dim_);
  task->Fetch("rnn_layers", &rnn_layers_);
  task->Fetch("rnn_type", &rnn_type_);
  task->Fetch("rnn_bidir", &rnn_bidir_);
  task->Fetch("rnn_highways", &rnn_highways_);

  task->Fetch("seed", &seed_);
  task->Fetch("batch_size", &batch_size_);
  task->Fetch("learning_rate", &learning_rate_);
  task->Fetch("min_learning_rate", &min_learning_rate_);
  task->Fetch("learning_rate_cliff", &learning_rate_cliff_);
  task->Fetch("dropout", &dropout_);

  task->Fetch("skip_section_titles", &skip_section_titles_);

  // Save task parameters.
  for (auto &p : task->parameters()) {
    hparams_.emplace_back(p.name, p.value);
  }

  // Statistics.
  num_tokens_ = task->GetCounter("tokens");
  num_documents_ = task->GetCounter("documents");

  // Load commons store from file.
  for (Binding *binding : task->GetInputs("commons")) {
    LoadStore(binding->resource()->name(), &commons_);
  }

  // Open training and evaluation corpora.
  training_corpus_ =
    new DocumentCorpus(&commons_, task->GetInputFiles("training_corpus"));
  evaluation_corpus_ =
    new DocumentCorpus(&commons_, task->GetInputFiles("evaluation_corpus"));

  // Output file for model.
  auto *model_file = task->GetOutput("model");
  if (model_file != nullptr) {
    model_filename_ = model_file->resource()->name();
  }

  // Set up encoder lexicon.
  string normalization = task->Get("normalization", "d");
  spec_.lexicon.normalization = ParseNormalization(normalization);
  spec_.lexicon.threshold = task->Get("lexicon_threshold", 0);
  spec_.lexicon.max_prefix = task->Get("max_prefix", 0);
  spec_.lexicon.max_suffix = task->Get("max_suffix", 3);
  spec_.feature_padding = 16;

  // Set up word embeddings.
  spec_.word_dim = task->Get("word_dim", 32);
  auto *word_embeddings_input = task->GetInput("word_embeddings");
  if (word_embeddings_input != nullptr) {
    spec_.word_embeddings = word_embeddings_input->resource()->name();
  }
  spec_.train_word_embeddings = task->Get("train_word_embeddings", true);

  // Set up lexical back-off features.
  spec_.prefix_dim = task->Get("prefix_dim", 0);
  spec_.suffix_dim = task->Get("suffix_dim", 16);
  spec_.hyphen_dim = task->Get("hypen_dim", 8);
  spec_.caps_dim = task->Get("caps_dim", 8);
  spec_.punct_dim = task->Get("punct_dim", 8);
  spec_.quote_dim = task->Get("quote_dim", 8);
  spec_.digit_dim = task->Get("digit_dim", 8);

  // Set up RNNs.
  RNN::Spec rnn_spec;
  rnn_spec.type = static_cast<RNN::Type>(rnn_type_);
  rnn_spec.dim = rnn_dim_;
  rnn_spec.highways = rnn_highways_;
  rnn_spec.dropout = dropout_;
  encoder_.AddLayers(rnn_layers_, rnn_spec, rnn_bidir_);

  // Build parser model flow graph.
  Build(&flow_, true);
  optimizer_ = GetOptimizer(task);
  optimizer_->Build(&flow_);

  // Compile model.
  compiler_.Compile(&flow_, &model_);

  // Initialize model.
  model_.InitModelParameters(seed_);
  encoder_.Initialize(model_);
  optimizer_->Initialize(model_);
  commons_.Freeze();

  // Train model.
  Train(task, &model_);

  // Save final model.
  if (!model_filename_.empty()) {
    LOG(INFO) << "Writing tagger model to " << model_filename_;
    Save(model_filename_);
  }

  // Clean up.
  delete optimizer_;
}

void TaggerTrainer::Worker(int index, Network *model) {
  // Create instances.
  LexicalEncoderLearner encoder(encoder_);

  // Collect gradients.
  std::vector<Instance *> gradients;
  encoder.CollectGradients(&gradients);

  // Training loop.
  for (;;) {
    // Prepare next batch.
    for (auto *g : gradients) g->Clear();
    float epoch_loss = 0.0;
    int epoch_count = 0;

    for (int b = 0; b < batch_size_; b++) {
      // Get next training document.
      Store store(&commons_);
      Document *original = GetNextTrainingDocument(&store);
      CHECK(original != nullptr);
      num_documents_->Increment();
      num_tokens_->Increment(original->length());
      Document document(*original, false);

      for (SentenceIterator s(original); s.more(); s.next()) {
        // Skip section titles if requested.
        if (skip_section_titles_) {
          const Token &first = document.token(s.begin());
          if (first.style() & HEADING_BEGIN) continue;
        }

        // Run document through encoder to produce contextual token encodings.
        //auto *encodings = encoder.Compute(document, s.begin(), s.end());

        // TODO: implement tagger forward and backward propagration.
      }

      delete original;
    }

    // Update parameters.
    update_mu_.Lock();
    optimizer_->Apply(gradients);
    loss_sum_ += epoch_loss;
    loss_count_ += epoch_count;
    update_mu_.Unlock();

    // Check if we are done.
    if (EpochCompleted()) break;
  }
}

void TaggerTrainer::Tag(Document *document) const {
  // Parse each sentence of the document.
  for (SentenceIterator s(document); s.more(); s.next()) {
    // Skip section titles if requested.
    if (skip_section_titles_) {
      const Token &first = document->token(s.begin());
      if (first.style() & HEADING_BEGIN) continue;
    }

    // Run the lexical encoder for sentence.
    LexicalEncoderInstance encoder(encoder_);
    //auto *encodings = encoder.Compute(*document, s.begin(), s.end());

    // TODO: tag sentence.
  }
}

bool TaggerTrainer::Evaluate(int64 epoch, Network *model) {
  // Skip evaluation if there are no data.
  if (loss_count_ == 0) return true;

  // Compute average loss of epochs since last eval.
  float loss = loss_sum_ / loss_count_;
  float p = exp(-loss) * 100.0;
  loss_sum_ = 0.0;
  loss_count_ = 0;

  // Decay learning rate if loss increases.
  bool decay = prev_loss_ != 0.0 && prev_loss_ < loss;
  if (learning_rate_cliff_ != 0 && epoch >= learning_rate_cliff_) decay = true;
  if (learning_rate_ <= min_learning_rate_) decay = false;
  if (decay) learning_rate_ = optimizer_->DecayLearningRate();
  prev_loss_ = loss;

  LOG(INFO) << "epoch=" << epoch
            << " lr=" << learning_rate_
            << " loss=" << loss
            << " p=" << p;

  // Evaluate current model on held-out evaluation corpus.
  TaggerEvaulationCorpus corpus(this);
  FrameEvaluation::Output eval;
  FrameEvaluation::Evaluate(&corpus, &eval);
  FrameEvaluation::Benchmarks benchmarks;
  eval.GetBenchmarks(&benchmarks);
  for (const auto &benchmark : benchmarks) LOG(INFO) << benchmark.Summary();

  return true;
}

void TaggerTrainer::Checkpoint(int64 epoch, Network *model) {
  if (!model_filename_.empty()) {
    LOG(INFO) << "Checkpointing model to " << model_filename_;
    Save(model_filename_);
  }
}

void TaggerTrainer::Build(Flow *flow, bool learn) {
  // Build document input encoder.
  RNN::Variables rnn;
  if (learn) {
    Vocabulary::HashMapIterator vocab(words_);
    rnn = encoder_.Build(flow, spec_, &vocab, true);
  } else {
    rnn = encoder_.Build(flow, spec_, nullptr, false);
  }
  //int token_dim = rnn.output->elements();

  // TODO: build tagger model.
}

Document *TaggerTrainer::GetNextTrainingDocument(Store *store) {
  MutexLock lock(&input_mu_);
  Document *document = training_corpus_->Next(store);
  if (document == nullptr) {
    // Loop around if the end of the training corpus has been reached.
    training_corpus_->Rewind();
    document = training_corpus_->Next(store);
  }
  return document;
}

void TaggerTrainer::Save(const string &filename) {
  // Build model.
  Flow flow;
  Build(&flow, false);

  // Copy weights from trained model.
  model_.SaveParameters(&flow);

  // Save lexicon.
  encoder_.SaveLexicon(&flow);

  // Make tagger specification frame.
  Store store(&commons_);
  Builder spec(&store);

  // Save encoder spec.
  Builder encoder_spec(&store);
  encoder_spec.Add("type", "lexrnn");
  encoder_spec.Add("rnn", static_cast<int>(rnn_type_));
  encoder_spec.Add("dim", rnn_dim_);
  encoder_spec.Add("layers", rnn_layers_);
  encoder_spec.Add("bidir", rnn_bidir_);
  encoder_spec.Add("highways", rnn_highways_);
  spec.Set("encoder", encoder_spec.Create());

  // TODO: save decoder spec.

  // Save hyperparameters in flow.
  Builder params(&store);
  for (auto &p : hparams_) {
    params.Add(String(&store, p.first), p.second);
  }
  spec.Set("hparams", params.Create());

  // Save tagger spec in flow.
  StringEncoder encoder(&store);
  encoder.Encode(spec.Create());

  Flow::Blob *blob = flow.AddBlob("tagger", "frame");
  blob->data = flow.AllocateMemory(encoder.buffer());
  blob->size = encoder.buffer().size();

  // Save model to file.
  DCHECK(flow.IsConsistent());
  flow.Save(filename);
}

TaggerTrainer::TaggerEvaulationCorpus::TaggerEvaulationCorpus(
    TaggerTrainer *trainer) : trainer_(trainer) {
  trainer_->evaluation_corpus_->Rewind();
}

bool TaggerTrainer::TaggerEvaulationCorpus::Next(Store **store,
                                                 Document **golden,
                                                 Document **predicted) {
  // Create a store for both golden and tagged document.
  Store *local = new Store(&trainer_->commons_);

  // Read next document from corpus.
  Document *document = trainer_->evaluation_corpus_->Next(local);
  if (document == nullptr) {
    delete local;
    return false;
  }

  // Clone document without annotations.
  Document *tagged = new Document(*document, false);

  // Tag the document using the current model.
  trainer_->Tag(tagged);
  tagged->Update();

  // Return golden and predicted documents.
  *store = local;
  *golden = document;
  *predicted = tagged;

  return true;
}

REGISTER_TASK_PROCESSOR("tagger-trainer", TaggerTrainer);

}  // namespace nlp
}  // namespace sling


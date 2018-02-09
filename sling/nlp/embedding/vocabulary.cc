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

#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include "sling/file/textmap.h"
#include "sling/task/accumulator.h"
#include "sling/task/documents.h"
#include "sling/util/unicode.h"

namespace sling {
namespace nlp {

using namespace task;

// Process documents and output counts for normalized words in documents.
class EmbeddingVocabularyMapper : public DocumentProcessor {
 public:
  void Startup(Task *task) override {
    // Initialize accumulator.
    accumulator_.Init(output(), 1 << 24);
  }

  void Process(Slice key, const Document &document) override {
    // Output normalize token words.
    for (const Token &token : document.tokens()) {
      // Normalize token.
      string normalized;
      UTF8::Normalize(token.text(), &normalized);

      // Discard empty tokens and punctuation tokens.
      if (normalized.empty()) continue;
      if (UTF8::IsPunctuation(normalized)) continue;

      // Output normalized token word.
      accumulator_.Increment(normalized);
    }
  }

  void Flush(Task *task) override {
    accumulator_.Flush();
  }

 private:
  // Accumulator for word counts.
  Accumulator accumulator_;
};

REGISTER_TASK_PROCESSOR("embedding-vocabulary-mapper",
                        EmbeddingVocabularyMapper);

// Collect vocabulary and output text map with words and counts.
class EmbeddingVocabularyReducer : public SumReducer {
 public:
  void Start(Task *task) override {
    // Initialize sum reducer.
    SumReducer::Start(task);

    // Get max vocabulary size and threshold for discarding words.
    threshold_ = task->Get("threshold", 100);
    max_words_ = task->Get("max_words", 100000);

    // Add OOV item to vocabulary as the first entry.
    vocabulary_.emplace_back("<UNKNOWN>", 0);

    // Statistics.
    num_words_ = task->GetCounter("num_words");
    word_count_ = task->GetCounter("word_count");
    num_words_discarded_ = task->GetCounter("num_words_discarded");
  }

  void Aggregate(int shard, const Slice &key, uint64 sum) override {
    if (sum < threshold_) {
      // Add counts for discarded words to OOV entry.
      vocabulary_[0].count += sum;
      num_words_discarded_->Increment();
    } else {
      // Add entry to vocabulary.
      vocabulary_.emplace_back(key.str(), sum);
    }
    num_words_->Increment();
    word_count_->Increment(sum);
  }

  void Done(Task *task) override {
    // Sort word entries in decreasing frequency. The OOV entry is kept as the
    // first entry.
    std::sort(vocabulary_.begin() + 1, vocabulary_.end(),
      [](const Entry &a, const Entry &b) {
        return a.count > b.count;
      });

    // Add counts for all discarded entries to OOV.
    for (int i = max_words_; i < vocabulary_.size(); ++i) {
      vocabulary_[0].count += vocabulary_[i].count;
    }

    // Write vocabulary to output.
    int words = 0;
    for (auto &entry : vocabulary_) {
      if (++words > max_words_) break;
      Output(0, new Message(entry.word, std::to_string(entry.count)));
    }
  }

 private:
  // Word entity with count.
  struct Entry {
    Entry(const string &word, int64 count) : word(word), count(count) {}
    string word;
    int64 count;
  };

  // Threshold for discarding words.
  int threshold_;

  // Maximum number of words in vocabulary.
  int max_words_;

  // Vocabulary. The first item is the OOV item.
  std::vector<Entry> vocabulary_;

  // Statistics.
  Counter *num_words_ = nullptr;
  Counter *word_count_ = nullptr;
  Counter *num_words_discarded_ = nullptr;
};

REGISTER_TASK_PROCESSOR("embedding-vocabulary-reducer",
                        EmbeddingVocabularyReducer);

// Random number generator.
class Random {
 public:
  Random() : dist_(0.0, 1.0) {}

  float UniformFloat(float scale, float bias) {
    return dist_(prng_) * scale + bias;
  }

  float UniformInt(int n) {
    return dist_(prng_) * n;
  }

 private:
  std::mt19937 prng_;
  std::uniform_real_distribution<float> dist_;
};

// Embedding model with input, hidden, and output layer.
class EmbeddingModel {
 public:
  // Initializes model.
  void Init(int inputs, int hidden, int outputs) {
    // Store dimensions.
    inputs_ = inputs;
    hidden_ = hidden;
    outputs_ = outputs;

    // Allocate weight matrices.
    w0_.resize(inputs * hidden);
    w1_.resize(hidden * outputs);

    // Initialize layer 0 with random weights.
    Random rnd;
    float *wptr = w0_.data();
    float *wend = wptr + inputs * hidden;
    while (wptr < wend) *wptr++ = rnd.UniformFloat(1.0, -0.5);
  }

  // Adds layer 0 weight vector to vector, v = v + w0[index].
  void AddLayer0(int index, std::vector<float> *v) const {
    DCHECK_GE(index, 0);
    DCHECK_LT(index, inputs_);
    const float *wptr = w0_.data() + index * hidden_;
    const float *wend = wptr + hidden_;
    float *vptr = v->data();
    while (wptr < wend) *vptr++ += *wptr++;
  }

  // Adds layer 1 weight vector to vector, v = v + s * w1_i.
  void AddLayer1(int index, float scalar, std::vector<float> *v) const {
    DCHECK_GE(index, 0);
    DCHECK_LT(index, outputs_);
    const float *wptr = w1_.data() + index * hidden_;
    const float *wend = wptr + hidden_;
    float *vptr = v->data();
    while (wptr < wend) *vptr++ += *wptr++ * scalar;
  }

  // Computes dot product between input vector and layer 1 weight vector.
  // Returns <v, w1_i>.
  float DotLayer1(int index, const std::vector<float> &v) const {
    DCHECK_GE(index, 0);
    DCHECK_LT(index, outputs_);
    float sum = 0.0;
    const float *wptr = w1_.data() + index * hidden_;
    const float *wend = wptr + hidden_;
    const float *vptr = v.data();
    while (wptr < wend) sum += *vptr++ * *wptr++;
    return sum;
  }

  // Updates layer 0 weights. w0_i = w0_i + v.
  void UpdateLayer0(int index, const std::vector<float> &v) {
    DCHECK_GE(index, 0);
    DCHECK_LT(index, inputs_);
    float *wptr = w0_.data() + index * hidden_;
    float *wend = wptr + hidden_;
    const float *vptr = v.data();
    while (wptr < wend) *wptr++ += *vptr++;
  }

  // Updates layer 1 weights. w1_i = w1_i + s * v
  void UpdateLayer1(int index, float scalar, const std::vector<float> &v) {
    DCHECK_GE(index, 0);
    DCHECK_LT(index, outputs_);
    float *wptr = w1_.data() + index * hidden_;
    float *wend = wptr + hidden_;
    const float *vptr = v.data();
    while (wptr < wend) *wptr++ += *vptr++ * scalar;
  }

  // Returns layer 1 weight vector (w1_i).
  void GetLayer1(int index, std::vector<float> *v) const {
    DCHECK_GE(index, 0);
    DCHECK_LT(index, outputs_);
    const float *wptr = w1_.data() + index * hidden_;
    const float *wend = wptr + hidden_;
    float *vptr = v->data();
    while (wptr < wend) *vptr++ = *wptr++;
  }

  // Free up memory used by network.
  void Clear() {
    w0_.clear();
    w1_.clear();
  }

 private:
  // Model dimensions.
  int inputs_;        // number of input nodes
  int hidden_;        // number of hidden nodes
  int outputs_;       // number of output nodes

  // Weight matrices represented as arrays:
  //   w0[i][h] ==> w0[i * hidden + h]
  //   w1[h][o] ==> w1[o * hidden + h]
  std::vector<float> w0_;  // weight matrix from feature input to hidden layer
  std::vector<float> w1_;  // weight matrix from hidden layer to entity output
};

// Vocabulary sampling.
class VocabularySampler {
 public:
  // Load vocabulary table.
  void Load(const string &filename) {
    // Read words and frequencies.
    TextMapInput input(filename);
    double sum = 0.0;
    int index;
    string word;
    int64 count;
    while (input.Read(&index, &word, &count)) {
      vocabulary_.push_back(word);
      permutation_.emplace_back(index, count);
      sum += count;
    }

    // Shuffle entities (Fisher–Yates shuffle).
    int n = permutation_.size();
    Random rnd;
    for (int i = 0; i < n - 1; ++i) {
      int j = rnd.UniformInt(n - i);
      std::swap(permutation_[i], permutation_[i + j]);
    }

    // Convert counts to accumulative distribution.
    double acc = 0.0;
    for (int i = 0; i < n; ++i) {
      acc += permutation_[i].probability / sum;
      permutation_[i].probability = acc;
    }
  }

  // Sample word according to distribution.
  int Sample(float p) const {
    int n = permutation_.size();
    int low = 0;
    int high = n - 1;
    while (low < high) {
      int center = (low + high) / 2;
      if (permutation_[center].probability < p) {
        low = center + 1;
      } else {
        high = center;
      }
    }
    return permutation_[low].index;
  }

  // Clear data.
  void Clear() {
    vocabulary_.clear();
    permutation_.clear();
  }

  // Return word from vocabulary.
  const string &GetWord(int index) const { return vocabulary_[index]; }

 private:
  struct Element {
    Element(int i, float p) : index(i), probability(p) {}
    int index;
    float probability;
  };

  std::vector<string> vocabulary_;
  std::vector<Element> permutation_;
};

// Trainer for word embeddings model. The trainer support running training on
// multiple threads concurrently. While this can significantly speed up
// the processing time, this will also lead to non-determinism because of
// concurrent access to shared data structure, i.e. the weight matrices in the
// network. However, the updates are usually small, so in practice these unsafe
// updates are usually not harmful and adding mutexes to serialize access to the
// network slows down training considerably.
#if 0
class WordEmbeddingTrainer : public Task {
 public:
  // One shard of the input data.
  struct Shard {
    int index;            // shard index
    string filename;      // shard file name
    int epoch = 0;        // current training epoch
    int64 total = 0;      // instances per epoch (0 if yet unknown)
    int64 processed = 0;  // total number instances processed
  };

  // Sets up inputs for training.
  void Setup(TaskContext *context) override {
    // Set up inputs.
    training_data_input_ =
        context->GetMultiInput("training-data",
                               "recordio",
                               "LabeledFeatureVector");

    entities_input_ = context->GetMultiInput("entities", "textmap", "count");
    dimensions_input_ = context->GetMultiInput("dimensions", "text", "");
    embeddings_output_ = context->GetOutput("embeddings", "sstable", "profile");
  }

  // Runs training of embedding net.
  bool Run(TaskContext *context) override {
    // Get training parameters.
    context->Fetch("iterations", &iterations_);
    context->Fetch("negative", &negative_);
    context->Fetch("learning_rate", &learning_rate_);
    context->Fetch("min_learning_rate", &min_learning_rate_);
    context->Fetch("embedding_size", &embedding_size_);

    // Read entity and feature dimensions.
    ReadDimensions();

    // Load entity sampler.
    sampler_.Load(*entities_input_);

    // Allocate embedding network.
    net_.Init(num_features_, embedding_size_, num_entities_);

    // Get file names for training data.
    std::vector<string> filenames;
    TaskContext::GetInputFiles(*training_data_input_, &filenames);
    shards_.resize(filenames.size());
    for (int i = 0; i < shards_.size(); ++i) {
      shards_[i].index = i;
      shards_[i].filename = filenames[i];
    }

    // Start training threads. Use one thread per input shard.
    ThreadPool *pool = new ThreadPool("EntityVectorTrainer", shards_.size());
    start_time_ = time(nullptr);
    pool->StartWorkers();
    for (int i = 0; i < shards_.size(); ++i) {
      pool->Add(NewCallback(this, &EntityVectorTrainer::Worker, &shards_[i]));
    }

    // Wait for training threads to complete.
    delete pool;

    // Get entity embeddings.
    SSTableWriter writer;
    std::vector<float> embedding(embedding_size_);
    for (int i = 0; i < num_entities_; ++i) {
      const string &mid = sampler_.GetMid(i);
      EntityProfile profile;
      profile.set_mid(mid);
      net_.GetLayer1(i, &embedding);
      for (float f : embedding) profile.add_embedding(f);
      writer.Add(mid, profile.SerializeAsString());
    }

    // Write profiles with embeddings to sstable.
    string output_filename = TaskContext::OutputFile(*embeddings_output_);
    SSTableBuilderOptions options;
    options.set_attribute_file(false);
    writer.Write(output_filename, options);

    // Clean up.
    net_.Clear();
    sampler_.Clear();
    return true;
  }

  // Worker thread for training embedding network.
  void Worker(Shard *shard) {
    MTRandom rnd(shard->index);
    LabeledFeatureVector instance;
    double alpha = learning_rate_;
    std::vector<float> hidden(embedding_size_);
    std::vector<float> error(embedding_size_);
    recordio::RecordReader input(
        file::OpenOrDie(shard->filename, "r", file::Defaults()));
    while (true) {
      // Read next instance from input.
      if (!input.Read(&instance)) {
        if (++shard->epoch < iterations_) {
          // Seek back to the begining.
          input.Seek(0);
          if (shard->total == 0) shard->total = shard->processed;

          // Update learning rate.
          alpha = learning_rate_ * (1 - shard->epoch / iterations_);
          if (alpha < min_learning_rate_) alpha = min_learning_rate_;
          continue;
        } else {
          break;
        }
      }
      shard->processed++;
      if (instance.feature_size() == 0) continue;

      // Propagate input to hidden layer.
      for (int i = 0; i < hidden.size(); ++i) hidden[i] = 0;
      for (int i = 0; i < error.size(); ++i) error[i] = 0;
      for (auto f : instance.feature()) {
        net_.AddLayer0(f, &hidden);
      }
      for (int i = 0; i < hidden.size(); ++i) {
        hidden[i] /= instance.feature_size();
      }

      // Propagate hidden to output. This is done for both the positive instance
      // (d=0) and randomly sampled negative samples (d>0).
      for (int d = 0; d < negative_ + 1; ++d) {
        // Select target entity for positive/negative instance.
        int target;
        float label;
        if (d == 0) {
          target = instance.label();
          label = 1;
        } else {
          target = sampler_.Sample(rnd.RandFloat());
          label = 0;
        }

        // Compute gradient.
        float f = net_.DotLayer1(target, hidden);
        float g = (label - sigmoid(f)) * alpha;

        // Propagate errors from output to hidden.
        net_.AddLayer1(target, g, &error);

        // Learn weights from hidden to output.
        net_.UpdateLayer1(target, g, hidden);
      }

      // Propagate hidden to input.
      for (auto f : instance.feature()) {
        net_.UpdateLayer0(f, error);
      }
    }
  }

  // Returns training progress status.
  void GetStatusMessage(string *message) override {
    int64 processed = 0;
    int64 total = 0;
    int epoch = iterations_;
    bool totals_ready = true;
    for (const Shard &shard : shards_) {
      processed += shard.processed;
      total += shard.total;
      if (shard.epoch < epoch) epoch = shard.epoch;
      if (shard.total == 0) totals_ready = false;
    }
    double progress = 0.0;
    if (totals_ready) {
      progress = static_cast<double>(processed) / (total * iterations_);
    }
    *message =
        StringPrintf("Epoch %d/%d, %lld/%lld instances, %.1f%%, %lld/sec",
                     epoch + 1, iterations_,
                     processed, total * iterations_,
                     progress * 100.0,
                     processed / (time(nullptr) - start_time_));
  }

  // Reads entity and feature space dimensions.
  void ReadDimensions() {
    string filename = TaskContext::InputFile(*dimensions_input_);
    string contents;
    CHECK_OK(file::GetContents(filename, &contents, file::Defaults()));
    const std::vector<string> fields =
        absl::StrSplit(contents, ' ', absl::SkipEmpty());
    CHECK_EQ(fields.size(), 2);
    CHECK(safe_strto32(fields[0], &num_entities_));
    CHECK(safe_strto32(fields[1], &num_features_));
  }

 private:
  // Input for training data.
  TaskInput *training_data_input_ = nullptr;

  // Input for entity distribution.
  TaskInput *entities_input_ = nullptr;

  // Input for entity and feature space dimensions.
  TaskInput *dimensions_input_ = nullptr;

  // Output for profiles with embeddings.
  TaskOutput *embeddings_output_ = nullptr;

  // Training parameters.
  int iterations_ = 15;                // number of training iterations
  int negative_ = 25;                  // negative examples per positive example
  double learning_rate_ = 0.025;       // learning rate
  double min_learning_rate_ = 0.0001;  // minimum learning rate
  int embedding_size_ = 200;           // size of embedding vectors

  // Number of entities and feature.
  int num_entities_;
  int num_features_;

  // Training data shards.
  std::vector<Shard> shards_;

  // Neural network for training.
  EntityNetwork net_;

  // Entity sampler for negative examples.
  EntitySampler sampler_;

  // Starting time (used for speed computation).
  int64 start_time_ = 0;
};

REGISTER_TASK_PROCESSOR("word-embedding-trainer", WordEmbeddingTrainer);
#endif

}  // namespace nlp
}  // namespace sling


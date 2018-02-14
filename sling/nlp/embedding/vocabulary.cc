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
#include <algorithm>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "sling/file/recordio.h"
#include "sling/file/textmap.h"
#include "sling/frame/serialization.h"
#include "sling/nlp/document/document.h"
#include "sling/task/accumulator.h"
#include "sling/task/documents.h"
#include "sling/task/process.h"
#include "sling/util/embeddings.h"
#include "sling/util/thread.h"
#include "sling/util/unicode.h"

namespace sling {
namespace nlp {

using namespace task;

// Sigmoid function.
static float sigmoid(float x) {
  return 1.0 / (1.0 + expf(-x));
}

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
  // Word entry with count.
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

  void seed(int seed) { prng_.seed(seed); }

  float UniformProb() {
    return dist_(prng_);
  }

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
  std::vector<float> w0_;  // weight matrix from input to hidden layer
  std::vector<float> w1_;  // weight matrix from hidden layer to output
};

// Vocabulary sampling.
class VocabularySampler {
 public:
  // Load vocabulary table.
  void Load(const string &filename, float subsampling) {
    // Read words and frequencies.
    TextMapInput input(filename);
    double sum = 0.0;
    int index;
    string word;
    int64 count;
    while (input.Read(&index, &word, &count)) {
      if (word == "<UNKNOWN>") oov_ = index;
      dictionary_[word] = index;
      entry_.emplace_back(word, count);
      permutation_.emplace_back(index, count);
      sum += count;
    }
    threshold_ = subsampling * sum;

    // Shuffle words (Fisher-Yates shuffle).
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

  // Look up word in dictionary. Return OOV for unknown words.
  int Lookup(const string &word) const {
    string normalized;
    UTF8::Normalize(word, &normalized);
    auto f = dictionary_.find(normalized);
    return f != dictionary_.end() ? f->second : oov_;
  }

  // Sample word according to distribution. Used for sampling negative examples.
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

  // Sub-sampling probability for word. Used for sub-sampling words in
  // sentences for skip-grams. This implements the sub-sampling strategy from
  // Mikolov 2013.
  float SubsamplingProbability(int index) const {
    float count = entry_[index].count;
    return (sqrt(count / threshold_) + 1.0) * threshold_ / count;
  }

  // Clear data.
  void Clear() {
    dictionary_.clear();
    entry_.clear();
    permutation_.clear();
  }

  // Return the number of word in the vocabulary.
  size_t size() const { return dictionary_.size(); }

  // Get word for index.
  const string &word(int index) const { return entry_[index].word; }

 private:
  struct Entry {
    Entry(const string &word, float count) : word(word), count(count) {}
    string word;
    float count;
  };

  struct Element {
    Element(int i, float p) : index(i), probability(p) {}
    int index;
    float probability;
  };

  // Mapping from word to vocabulary index.
  std::unordered_map<string, int> dictionary_;

  // Word list.
  std::vector<Entry> entry_;

  // Permutaion of words for sampling.
  std::vector<Element> permutation_;

  // Threshold for sub-sampling words
  float threshold_;

  // Entry for unknown words.
  int oov_ = 0;
};

// Trainer for word embeddings model. The trainer supports running training on
// multiple threads concurrently. While this can significantly speed up
// the processing time, this will also lead to non-determinism because of
// concurrent access to shared data structure, i.e. the weight matrices in the
// network. However, the updates are usually small, so in practice these unsafe
// updates are usually not harmful and adding mutexes to serialize access to the
// model slows down training considerably.
class WordEmbeddingTrainer : public Process {
 public:
  // Run training of embedding net.
  void Run(Task *task) override {
    // Get training parameters.
    task->Fetch("iterations", &iterations_);
    task->Fetch("negative", &negative_);
    task->Fetch("window", &window_);
    task->Fetch("learning_rate", &learning_rate_);
    task->Fetch("min_learning_rate", &min_learning_rate_);
    task->Fetch("embedding_dims", &embedding_dims_);
    task->Fetch("subsampling", &subsampling_);

    // Load vocabulary.
    vocabulary_.Load(task->GetInputFile("vocabulary"), subsampling_);
    int vocabulary_size = vocabulary_.size();

    // Allocate embedding model.
    model_.Init(vocabulary_size, embedding_dims_, vocabulary_size);

    // Initialize commons store.
    commons_ = new Store();
    docnames_ = new DocumentNames(commons_);
    commons_->Freeze();

    // Statistics.
    num_documents_ = task->GetCounter("num_documents");
    total_documents_ = task->GetCounter("total_documents");
    num_tokens_ = task->GetCounter("num_tokens");
    num_instances_ = task->GetCounter("num_instances");
    epochs_completed_ = task->GetCounter("epochs_completed");

    // Start training threads. Use one worker thread per input file.
    std::vector<string> filenames = task->GetInputFiles("documents");
    WorkerPool pool;
    pool.Start(filenames.size(), [this, filenames](int index) {
      Worker(index, filenames[index]);
    });

    // Wait until workers completes.
    pool.Join();

    // Write embeddings to output file.
    const string &output_filename = task->GetOutputFile("output");
    EmbeddingWriter writer(output_filename, vocabulary_size, embedding_dims_);
    std::vector<float> embedding(embedding_dims_);
    for (int i = 0; i < vocabulary_size; ++i) {
      const string &word = vocabulary_.word(i);
      model_.GetLayer1(i, &embedding);
      writer.Write(word, embedding);
    }
    CHECK(writer.Close());

    // Clean up.
    model_.Clear();
    vocabulary_.Clear();
    docnames_->Release();
    delete commons_;
    docnames_ = nullptr;
    commons_ = nullptr;
  }

  // Worker thread for training embedding model.
  void Worker(int index, const string &filename) {
    Random rnd;
    rnd.seed(index);
    float alpha = learning_rate_;
    int epoch = 0;
    std::vector<int> words;
    std::vector<int> features;
    std::vector<float> hidden(embedding_dims_);
    std::vector<float> error(embedding_dims_);

    RecordFileOptions options;
    RecordReader input(filename, options);
    Record record;
    for (;;) {
      // Check for end of corpus.
      if (input.Done()) {
        epochs_completed_->Increment();
        if (++epoch < iterations_) {
          // Seek back to the begining.
          input.Rewind();

          // Update learning rate.
          float progress = static_cast<float>(epoch) / iterations_;
          alpha = learning_rate_ * (1.0 - progress);
          if (alpha < min_learning_rate_) alpha = min_learning_rate_;
          continue;
        } else {
          break;
        }
      }

      // Read next record from input.
      CHECK(input.Read(&record));
      num_documents_->Increment();
      if (epoch == 0) total_documents_->Increment();

      // Create document.
      Store store(commons_);
      StringDecoder decoder(&store, record.value.data(), record.value.size());
      Document document(decoder.Decode().AsFrame(), docnames_);
      num_tokens_->Increment(document.num_tokens());

      // Go over each sentence in the document.
      for (SentenceIterator s(&document); s.more(); s.next()) {
        // Get all the words in the sentence with sub-sampling.
        words.clear();
        for (int t = s.begin(); t < s.end(); ++t) {
          // Skip punctuation tokens.
          const string &word = document.token(t).text();
          if (UTF8::IsPunctuation(word)) continue;

          // Sub-sample words.
          int index = vocabulary_.Lookup(word);
          if (rnd.UniformProb() < vocabulary_.SubsamplingProbability(index)) {
            words.push_back(index);
          }
        }

        // Use each word in the sentence as a training example.
        for (int pos = 0; pos < words.size(); ++pos) {
          // Get features from window around word.
          features.clear();
          for (int i = pos - window_; i <= pos + window_; ++i) {
            if (i == pos) continue;
            if (i < 0) continue;
            if (i >= words.size()) continue;
            features.push_back(words[i]);
          }
          if (features.empty()) continue;
          num_instances_->Increment();

          // Propagate input to hidden layer.
          for (int i = 0; i < hidden.size(); ++i) hidden[i] = 0;
          for (int i = 0; i < error.size(); ++i) error[i] = 0;
          for (auto f : features) {
            model_.AddLayer0(f, &hidden);
          }
          for (int i = 0; i < hidden.size(); ++i) {
            hidden[i] /= features.size();
          }

          // Propagate hidden to output. This is done for both the positive
          // instance (d=0) and randomly sampled negative samples (d>0).
          for (int d = 0; d < negative_ + 1; ++d) {
            // Select target word for positive/negative instance.
            int target;
            float label;
            if (d == 0) {
              target = words[pos];
              label = 1;
            } else {
              target = vocabulary_.Sample(rnd.UniformProb());
              label = 0;
            }

            // Compute output.
            float f = model_.DotLayer1(target, hidden);

            // Compute gradient.
            float g = (label - sigmoid(f)) * alpha;

            // Propagate errors from output to hidden.
            model_.AddLayer1(target, g, &error);

            // Learn weights from hidden to output.
            model_.UpdateLayer1(target, g, hidden);
          }

          // Propagate hidden to input.
          for (auto f : features) {
            model_.UpdateLayer0(f, error);
          }
        }
      }
    }
  }

 private:
  // Training parameters.
  int iterations_ = 5;                 // number of training iterations
  int negative_ = 5;                   // negative examples per positive example
  int window_ = 5;                     // window size for skip-grams
  double learning_rate_ = 0.025;       // learning rate
  double min_learning_rate_ = 0.0001;  // minimum learning rate
  int embedding_dims_ = 200;           // size of embedding vectors
  double subsampling_ = 1e-3;          // sub-sampling rate

  // Neural network for training.
  EmbeddingModel model_;

  // Vocabulary for embeddings.
  VocabularySampler vocabulary_;

  // Commons store.
  Store *commons_ = nullptr;
  const DocumentNames *docnames_ = nullptr;

  // Statistics.
  Counter *num_documents_ = nullptr;
  Counter *total_documents_ = nullptr;
  Counter *num_tokens_ = nullptr;
  Counter *num_instances_ = nullptr;
  Counter *epochs_completed_ = nullptr;
};

REGISTER_TASK_PROCESSOR("word-embedding-trainer", WordEmbeddingTrainer);

}  // namespace nlp
}  // namespace sling


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

#include <string>
#include <vector>

#include "sling/task/accumulator.h"
#include "sling/task/documents.h"
#include "sling/util/unicode.h"

namespace sling {
namespace nlp {

// Process documents and output counts for normalized words in documents.
class EmbeddingVocabularyMapper : public task::DocumentProcessor {
 public:
  void Startup(task::Task *task) override {
    // Initialize accumulator.
    accumulator_.Init(output());
  }

  void Process(Slice key, const Document &document) override {
    //accumulator_.Increment(name, count);
  }

  void Flush(task::Task *task) override {
    accumulator_.Flush();
  }

 private:
  // Accumulator for word counts.
  task::Accumulator accumulator_;
};

REGISTER_TASK_PROCESSOR("embedding-vocabulary-mapper",
                        EmbeddingVocabularyMapper);

// Collect vocabulary and output text map with words and counts.
class EmbeddingVocabularyReducer : public task::SumReducer {
 public:
  void Start(task::Task *task) override {
    // Get threshold for discarding words.
    threshold_ = task->Get("threshold", 100);

    // Add OOV item to vocabulary as the first entry.
    vocabulary_.emplace_back("<UNKNOWN>", 0);
  }

  void Aggregate(int shard, const Slice &key, uint64 sum) override {
    if (sum < threshold_) {
      // Add counts for discarded words to OOV entry.
      vocabulary_[0].count += sum;
    } else {
      // Add entry to vocabulary.
      vocabulary_.emplace_back(string(key.data(), key.size()), sum);
    }
  }

  void Done(task::Task *task) override {
    // Write vocabulary to output.
    for (auto &entry : vocabulary_) {
      Output(0, new task::Message(entry.word, std::to_string(entry.count)));
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

  // Vocabulary. The first item is the OOV item.
  std::vector<Entry> vocabulary_;
};

REGISTER_TASK_PROCESSOR("embedding-vocabulary-reducer",
                        EmbeddingVocabularyReducer);

}  // namespace nlp
}  // namespace sling


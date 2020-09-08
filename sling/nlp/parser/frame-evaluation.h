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

// Frame evaluation.

#ifndef SLING_NLP_PARSER_FRAME_EVALUATION_H_
#define SLING_NLP_PARSER_FRAME_EVALUATION_H_

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sling/nlp/document/document.h"
#include "sling/frame/object.h"
#include "sling/frame/store.h"

namespace sling {
namespace nlp {

// Parallel corpus for evaluation.
class ParallelCorpus {
 public:
  virtual ~ParallelCorpus() = default;

  // Read next pair of documents. Return false when there are no more documents.
  // Ownership of the store and documents is transferred to the caller.
  virtual bool Next(Store **store, Document **golden, Document **predicted) = 0;

  // Return global store for corpus.
  virtual Store *Commons() = 0;
};

// Compute precision and recall for frame annotations in an annotated corpus
// compared to a gold-standard corpus. This evaluation does not take thematic
// frames into account yet.
class FrameEvaluation {
 public:
  // Pair of frames.
  typedef std::pair<Handle, Handle> FramePair;

  // Named scores.
  typedef std::pair<string, float> Score;
  typedef std::vector<Score> Scores;

  // Frame alignment.
  class Alignment : public HandleMap<Handle> {
   public:
    // Maps source frame to target frame. Returns true if the mapping was added
    // to the alignment.
    bool Map(Handle source, Handle target);

    // Returns the target that frame is mapped to or nil.
    Handle Lookup(Handle handle) const;

   private:
    HandleSet targets_;
  };

  // Statistics for computing accuracy for one metric.
  struct Metric {
    // Adds one correct/wrong prediction to metric.
    void prediction(bool good) {
      if (good) {
        correct++;
      } else {
        wrong++;
      }
    }

    // Adds another metric to this one.
    void add(const Metric &other) {
      correct += other.correct;
      wrong += other.wrong;
    }

    // Total number of predictions.
    int total() const {
      return correct + wrong;
    }

    // Prediction accuracy.
    double accuracy() const {
      if (total() == 0) return 0;
      return static_cast<double>(correct) / static_cast<double>(total());
    }

    // Check if metric is being used.
    bool used() const { return correct > 0 || wrong > 0; }

    // Number of correct and wrong predictions.
    int correct = 0;
    int wrong = 0;
  };

  // Benchmark with precision and recall.
  struct Benchmark {
    Benchmark() {}
    Benchmark(const string &name) : name(name) {}

    // Computes F-score from precision and recall.
    double fscore() const {
      double p = precision.accuracy();
      double r = recall.accuracy();
      if (p == 0 && r == 0) return 0;
      return 2 * p * r / (p + r);
    }

    // Adds another benchmark to this one.
    void add(const Benchmark other) {
      recall.add(other.recall);
      precision.add(other.precision);
    }

    // Get scores for benchmark.
    void GetScores(Scores *scores) const;

    // Return benchmark summary with precision, recall, and F1.
    string Summary(int width = 6) const;

    // Check if benchmark is being used.
    bool used() const { return recall.used() || precision.used(); }

    // Precision and recall statistics for benchmark.
    string name;
    Metric recall;
    Metric precision;
  };

  typedef std::vector<Benchmark> Benchmarks;
  typedef HandleMap<Benchmark> BenchmarkMap;

  // Holds evaluation output.
  struct Output {
    // Benchmarks of various aspects.
    Benchmark mention{"SPAN"};
    Benchmark frame{"FRAME"};
    Benchmark pair{"PAIR"};
    Benchmark edge{"EDGE"};
    Benchmark role{"ROLE"};
    Benchmark type{"TYPE"};
    Benchmark label{"LABEL"};
    Benchmark slot{"SLOT"};
    Benchmark combined{"TOTAL"};

    // Type and role benchmarks.
    BenchmarkMap types;
    BenchmarkMap roles;

    // Counters.
    int64 num_golden_spans = 0;
    int64 num_predicted_spans = 0;
    int64 num_golden_frames = 0;
    int64 num_predicted_frames = 0;

    // Get evaluation scores.
    void GetScores(Scores *scores) const;

    // Get all used benchmarks.
    void GetBenchmarks(Benchmarks *benchmarks) const;
  };

  // Evaluates parallel corpus (gold and test) and returns the evaluation in
  // 'output'.
  static void Evaluate(ParallelCorpus *corpus, Output *output);

  // Evaluates two equal-sized corpora of files (gold and test) and returns
  // the evaluation in 'output'.
  static void Evaluate(Store *commons,
                       const string &gold_file_pattern,
                       const string &test_file_pattern,
                       Output *output);

 private:
  // Computes mention alignment from source to target.
  static void AlignMentions(const Document &source,
                            const Document &target,
                            Alignment *alignment);

  // Computes alignment between evoked frames for each mention.
  static void AlignEvokes(Store *store,
                          const Alignment &mentions,
                          Alignment *alignment);

  // Align evoked frames in mention source with evoked frames in mention target.
  static void AlignEvoke(const Frame &source,
                         const Frame &target,
                         Handle n_evokes,
                         Alignment *alignment);

  // Extends frame alignment to all remaining frames reachable from the initial
  // alignment with the evoked frames.
  static void AlignFrames(Store *store, Alignment *alignment);

  // Computes alignment accuracy.
  static void AlignmentAccuracy(const Alignment &alignment, Metric *metric);

  // Computes role accuracy.
  static void RoleAccuracy(Store *store, const Alignment &alignment,
                           Metric *pair, Metric *edge, Metric *role,
                           Metric *type, Metric *label,
                           BenchmarkMap *roles, bool recall);

  // Computes type accuracy.
  static void TypeAccuracy(Store *store, const Alignment &alignment,
                           BenchmarkMap *types, bool recall);

  // Counts the number of slots with a given name.
  static int SlotCount(const Frame &f, Handle name);

  // Checks if frame has a slot with a given name and value.
  static bool HasSlot(const Frame &f, Handle name, Handle value);

  // Checks if frame has a slot with a given value.
  static bool HasValue(const Frame &f, Handle value);
};

}  // namespace nlp
}  // namespace sling

#endif // SLING_NLP_PARSER_FRAME_EVALUATION_H_

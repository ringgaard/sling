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

#include "sling/nlp/parser/frame-evaluation.h"

#include <algorithm>

#include "sling/base/logging.h"
#include "sling/frame/serialization.h"
#include "sling/nlp/document/document-corpus.h"
#include "sling/string/strcat.h"
#include "sling/string/printf.h"

namespace sling {
namespace nlp {

// Parallel corpus for file-based document source.
class FileParallelCorpus : public ParallelCorpus {
 public:
  // Open corpora.
  FileParallelCorpus(Store *commons,
                     const string &gold_file_pattern,
                     const string &test_file_pattern)
      :  commons_(commons),
         gold_corpus_(commons, gold_file_pattern),
         test_corpus_(commons, test_file_pattern) {}

  // Read next document pair from corpora.
  bool Next(Store **store, Document **golden, Document **predicted) override {
    *store = new Store(commons_);
    *golden = gold_corpus_.Next(*store);
    *predicted = test_corpus_.Next(*store);
    if (*golden == nullptr) {
      CHECK(*predicted == nullptr);
      delete *store;
      return false;
    } else {
      CHECK(*predicted != nullptr);
      return true;
    }
  }

  // Return commons store for corpus.
  Store *Commons() override { return commons_; }

 private:
  Store *commons_;               // commons store for documents
  DocumentCorpus gold_corpus_;   // corpus with gold annotations
  DocumentCorpus test_corpus_;   // corpus with predicted annotations
};

bool FrameEvaluation::Alignment::Map(Handle source, Handle target) {
  // Do not allow any previous mapping to be overwritten.
  if (!Lookup(source).IsNil()) return false;

  // Only allow target to be used once.
  if (!target.IsNil() && targets_.count(target) > 0) return false;

  // Add mapping to alignment.
  (*this)[source] = target;
  if (!target.IsNil()) targets_.insert(target);
  return true;
}

// Returns the target that frame is mapped to or nil.
Handle FrameEvaluation::Alignment::Lookup(Handle handle) const {
  auto f = find(handle);
  return f == end() ? Handle::nil() : f->second;
}

void FrameEvaluation::Evaluate(ParallelCorpus *corpus, Output *output) {
  // Benchmarks.
  auto &mention = output->mention;
  auto &frame = output->frame;
  auto &pair = output->pair;
  auto &edge = output->edge;
  auto &role = output->role;
  auto &type = output->type;
  auto &label = output->label;

  // Statistics counters.
  output->num_golden_spans = 0;
  output->num_predicted_spans = 0;
  output->num_golden_frames = 0;
  output->num_predicted_frames = 0;

  Store *store;
  Document *golden;
  Document *predicted;
  while (corpus->Next(&store, &golden, &predicted)) {
    CHECK_EQ(golden->num_tokens(), predicted->num_tokens());

    // Compute mention span alignments.
    Alignment g2p_mention_alignment;
    Alignment p2g_mention_alignment;
    AlignMentions(*golden, *predicted, &g2p_mention_alignment);
    AlignMentions(*predicted, *golden, &p2g_mention_alignment);

    // Compute evoked frame alignment.
    Alignment g2p_frame_alignment;
    Alignment p2g_frame_alignment;
    AlignEvokes(store, g2p_mention_alignment, &g2p_frame_alignment);
    AlignEvokes(store, p2g_mention_alignment, &p2g_frame_alignment);

    // Align frames that are not directly evoked from a span.
    AlignFrames(store, &g2p_frame_alignment);
    AlignFrames(store, &p2g_frame_alignment);

    // Compute mention precision and recall.
    AlignmentAccuracy(g2p_mention_alignment, &mention.recall);
    AlignmentAccuracy(p2g_mention_alignment, &mention.precision);

    // Compute frame precision and recall.
    AlignmentAccuracy(g2p_frame_alignment, &frame.recall);
    AlignmentAccuracy(p2g_frame_alignment, &frame.precision);

    // Compute role precision and recall.
    RoleAccuracy(store, g2p_frame_alignment,
                 &pair.recall, &edge.recall, &role.recall,
                 &type.recall, &label.recall,
                 &output->roles, true);
    RoleAccuracy(store, p2g_frame_alignment,
                 &pair.precision, &edge.precision, &role.precision,
                 &type.precision, &label.precision,
                 &output->roles, false);

    // Compute type precision and recall.
    TypeAccuracy(store, g2p_frame_alignment, &output->types, true);
    TypeAccuracy(store, p2g_frame_alignment, &output->types, false);

    // Update statistics.
    output->num_golden_spans += golden->num_spans();
    output->num_predicted_spans += predicted->num_spans();
    output->num_golden_frames += g2p_frame_alignment.size();
    output->num_predicted_frames += p2g_frame_alignment.size();

    delete golden;
    delete predicted;
    delete store;
  }

  // Compute the slot score as the sum of the type, role, and label scores.
  auto &slot = output->slot;
  slot.add(type);
  slot.add(role);
  slot.add(label);

  // Compute the combined score as the sum of the other scores.
  auto &combined = output->combined;
  combined.add(mention);
  combined.add(frame);
  combined.add(type);
  combined.add(role);
  combined.add(label);

  // Add labels to type and role benchmarks.
  for (auto &it : output->types) {
    Frame type(corpus->Commons(), it.first);
    it.second.name = type.GetString(Handle::name());
    if (it.second.name.empty()) {
      it.second.name = corpus->Commons()->DebugString(it.first);
    }
  }
  for (auto &it : output->roles) {
    Frame role(corpus->Commons(), it.first);
    it.second.name = role.GetString(Handle::name());
    if (it.second.name.empty()) {
      it.second.name = corpus->Commons()->DebugString(it.first);
    }
  }
}

void FrameEvaluation::Evaluate(Store *commons,
                               const string &gold_file_pattern,
                               const string &test_file_pattern,
                               FrameEvaluation::Output *output) {
  FileParallelCorpus corpus(commons, gold_file_pattern, test_file_pattern);
  Evaluate(&corpus, output);
}

void FrameEvaluation::Benchmark::GetScores(Scores *scores) const {
  double p = precision.accuracy();
  double r = recall.accuracy();
  double f1 = fscore();
  scores->emplace_back(StrCat(name, "_P+"), precision.correct);
  scores->emplace_back(StrCat(name, "_P-"), precision.wrong);
  scores->emplace_back(StrCat(name, "_R+"), recall.correct);
  scores->emplace_back(StrCat(name, "_R-"), recall.wrong);
  scores->emplace_back(StrCat(name, "_Precision"), p * 100.0);
  scores->emplace_back(StrCat(name, "_Recall"), r * 100.0);
  scores->emplace_back(StrCat(name, "_F1"), f1 * 100.0);
}

string FrameEvaluation::Benchmark::Summary(int width) const {
  double p = precision.accuracy() * 100.0;
  double r = recall.accuracy() * 100.0;
  double f1 = fscore() * 100.0;
  return StringPrintf("%*s P=%5.2f, R=%5.2f, F1=%5.2f",
                      -width, name.c_str(), p, r, f1);
}

void FrameEvaluation::Output::GetScores(Scores *scores) const {
  mention.GetScores(scores);
  frame.GetScores(scores);
  pair.GetScores(scores);
  edge.GetScores(scores);
  role.GetScores(scores);
  type.GetScores(scores);
  label.GetScores(scores);
  slot.GetScores(scores);
  combined.GetScores(scores);
  scores->emplace_back("#GOLDEN_SPANS", num_golden_spans);
  scores->emplace_back("#PREDICTED_SPANS", num_predicted_spans);
  scores->emplace_back("#GOLDEN_FRAMES", num_golden_frames);
  scores->emplace_back("#PREDICTED_FRAMES", num_predicted_frames);
}

void FrameEvaluation::Output::GetBenchmarks(Benchmarks *benchmarks) const {
  if (mention.used()) benchmarks->emplace_back(mention);
  if (frame.used()) benchmarks->emplace_back(frame);
  if (pair.used()) benchmarks->emplace_back(pair);
  if (edge.used()) benchmarks->emplace_back(edge);
  if (role.used()) benchmarks->emplace_back(role);
  if (type.used()) benchmarks->emplace_back(type);
  if (label.used()) benchmarks->emplace_back(label);
  if (slot.used()) benchmarks->emplace_back(slot);
  if (combined.used()) benchmarks->emplace_back(combined);
}

void FrameEvaluation::AlignMentions(const Document &source,
                                    const Document &target,
                                    Alignment *alignment) {
  // Iterate over all spans in source.
  for (Span *s : source.spans()) {
    // Try to find matching span in target.
    Span *t = target.GetSpan(s->begin(), s->end());
    if (t == nullptr) {
      // No matching span in target. Insert nil alignment.
      alignment->Map(s->mention().handle(), Handle::nil());
    } else {
      // Matching span found. Add mention pair to alignment.
      alignment->Map(s->mention().handle(), t->mention().handle());
    }
  }
}

void FrameEvaluation::AlignEvokes(Store *store,
                                  const Alignment &mentions,
                                  Alignment *alignment) {
  Handle n_evokes = store->Lookup("evokes");
  for (const auto &m : mentions) {
    if (m.second != Handle::nil()) {
      // Align source and target mentions.
      Frame source(store, m.first);
      Frame target(store, m.second);
      AlignEvoke(source, target, n_evokes, alignment);
    } else {
      // Add empty alignments for all frames evoked by the source.
      Frame source(store, m.first);
      for (const Slot &s : source) {
        if (s.name == n_evokes) {
          alignment->Map(s.value, Handle::nil());
        }
      }
    }
  }
}

void FrameEvaluation::AlignEvoke(const Frame &source,
                                 const Frame &target,
                                 Handle n_evokes,
                                 Alignment *alignment) {
  int source_evokes = SlotCount(source, n_evokes);
  int target_evokes = SlotCount(target, n_evokes);
  if (source_evokes == 1 && target_evokes == 1) {
    // Each span only evokes a single frame.
    alignment->Map(source.GetHandle(n_evokes), target.GetHandle(n_evokes));
  } else if (source_evokes > 0 && target_evokes > 0) {
    // Align evoked frames based on type.
    for (const Slot &s : source) {
      if (s.name != n_evokes) continue;

      // Get type for frame evoked by source.
      Frame source_frame(source.store(), s.value);
      Handle source_type = source_frame.GetHandle(Handle::isa());
      if (source_type.IsNil()) {
        alignment->Map(source_frame.handle(), Handle::nil());
        continue;
      }

      // Try to find frame evoked by target with same type.
      Handle match = Handle::nil();
      for (const Slot &t : target) {
        if (t.name != n_evokes) continue;
        Frame target_frame(target.store(), t.value);
        Handle target_type = target_frame.GetHandle(Handle::isa());
        if (target_type == source_type) {
          match = target_frame.handle();
          break;
        }
      }

      // Add alignment for frame evoked from source mention. This will be nil
      // if no match was found. This ensures that all frames evoked from
      // mentions will have an entry in the alignment.
      alignment->Map(source_frame.handle(), match);
    }
  } else if (source_evokes > 0) {
    // Add empty alignment for all source frames.
    for (const Slot &s : source) {
      if (s.name == n_evokes) {
        alignment->Map(s.value, Handle::nil());
      }
    }
  }
}

void FrameEvaluation::AlignFrames(Store *store, Alignment *alignment) {
  // Initialize queue of all the frame pairs where the slots still need to be
  // aligned.
  std::vector<FramePair> pending;
  for (auto it : *alignment) {
    if (!it.second.IsNil()) pending.push_back(it);
  }

  // Keep aligning the slots in the frame pairs in the pending queue.
  while (!pending.empty()) {
    // Get next pending frame from the queue.
    FramePair current = pending.back();
    pending.pop_back();
    Frame source(store, current.first);
    Frame target(store, current.second);

    // Try to find alignment for each slot in the source frame.
    for (const Slot &s : source) {
      // Skip special slots.
      if (s.name.IsId() || s.name.IsIsA() || s.name.IsIs()) continue;

      // Skip slots that do no refer to a local frame. These are typically
      // labels and not frame-to-frame roles.
      if (!s.value.IsLocalRef()) continue;
      Frame value(store, s.value);
      if (!value.IsFrame()) continue;

      // Skip if already aligned.
      if (!alignment->Lookup(value.handle()).IsNil()) continue;

      // Find corresponding role value in target.
      Handle h = target.GetHandle(s.name);

      // Add alignment for role value. An entry is added even in the case
      // where there is no target to ensure that all source frames will have
      // an entry in the alignment.
      if (!alignment->Map(value.handle(), h)) continue;

      // Add frame pair to the pending frame alignment queue.
      if (!h.IsNil()) pending.emplace_back(value.handle(), h);
    }
  }
}

void FrameEvaluation::AlignmentAccuracy(
    const Alignment &alignment, Metric *metric) {
  for (const auto &a : alignment) {
    metric->prediction(!a.second.IsNil());
  }
}

void FrameEvaluation::RoleAccuracy(
    Store *store, const Alignment &alignment,
    Metric *pair, Metric *edge, Metric *role,
    Metric *type, Metric *label,
    BenchmarkMap *roles, bool recall) {
  for (const auto &a : alignment) {
    Frame source(store, a.first);
    Frame target(store, a.second);

    // Try to find match target slot for each slot in the source frame.
    for (const Slot &s : source) {
      if (s.name.IsIsA()) {
        // Check type.
        type->prediction(HasSlot(target, Handle::isa(), s.value));
      } else if (s.name.IsId() || s.name.IsIs()) {
        // Ignore special roles.
      } else if (s.value.IsLocalRef()) {
        // Check frame-to-frame role.
        Handle value = alignment.Lookup(s.value);
        pair->prediction(!value.IsNil());
        edge->prediction(HasValue(target, value));
        role->prediction(HasSlot(target, s.name, value));

        if (s.name.IsGlobalRef()) {
          Benchmark &b = (*roles)[s.name];
          Metric &m = recall ? b.recall : b.precision;
          m.prediction(HasSlot(target, s.name, value));
        }
      } else {
        // Check label role.
        label->prediction(HasSlot(target, s.name, s.value));
      }
    }
  }
}

void FrameEvaluation::TypeAccuracy(Store *store, const Alignment &alignment,
                                   BenchmarkMap *types, bool recall) {
  for (const auto &a : alignment) {
    Frame source(store, a.first);
    Frame target(store, a.second);

    for (const Slot &s : source) {
      if (s.name.IsIsA()) {
        Benchmark &b = (*types)[s.value];
        Metric &m = recall ? b.recall : b.precision;
        m.prediction(HasSlot(target, Handle::isa(), s.value));
      }
    }
  }
}

int FrameEvaluation::SlotCount(const Frame &f, Handle name) {
  int n = 0;
  for (const Slot &s : f) {
    if (s.name == name) n++;
  }
  return n;
}

bool FrameEvaluation::HasSlot(const Frame &f, Handle name, Handle value) {
  if (f.invalid() || name.IsNil() || value.IsNil()) return false;
  for (const Slot &s : f) {
    if (s.name == name && s.value == value) return true;
  }
  return false;
}

bool FrameEvaluation::HasValue(const Frame &f, Handle value) {
  if (f.invalid() || value.IsNil()) return false;
  for (const Slot &s : f) {
    if (s.value == value) return true;
  }
  return false;
}

}  // namespace nlp
}  // namespace sling

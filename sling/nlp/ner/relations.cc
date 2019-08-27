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

#include "sling/nlp/document/annotator.h"
#include "sling/nlp/document/document.h"
#include "sling/nlp/kb/facts.h"

namespace sling {
namespace nlp {

using namespace task;

// Annotate relations between resolved mentions.
class RelationAnnotator : public Annotator {
 public:
  void Init(Task *task, Store *commons) override {
    // Initialize fact extractor.
    catalog_.Init(commons);
  }

  // Annotate relations in document.
  void Annotate(Document *document) override {
    LOG(INFO) << "==========================================================";
    // Process each sentence separately so we do not annotate relations between
    // mentions in different sentences.
    Store *store = document->store();
    for (SentenceIterator s(document); s.more(); s.next()) {
      // Find all resolved spans in sentence.
      std::vector<Mention> mentions;
      HandleSet targets;
      for (int t = s.begin(); t < s.end(); ++t) {
        // Get spans starting on this token.
        for (Span *span = document->GetSpanAt(t); span; span = span->parent()) {
          // Discard spans  we already have.
          bool existing = false;
          for (Mention &m : mentions) {
            if (m.span == span) {
              existing = true;
              break;
            }
          }
          if (existing) continue;

          // TEST: only add top-level mentions.
          if (span->parent() != nullptr) continue;

          // Add new mention.
          Mention mention;
          mention.span = span;
          mention.outer = span;
          while (mention.outer->parent() != nullptr) {
            mention.outer = mention.outer->parent();
          }
          mention.frame = span->evoked();
          if (mention.frame.IsNil()) continue;
          mention.item = store->Resolve(mention.frame);
          mentions.emplace_back(mention);
          targets.insert(mention.item);
        }
      }

      // Find facts for each mention that match a target in the sentence.
      LOG(INFO) << "Sentence: " << document->PhraseText(s.begin(), s.end());
      for (Mention &source : mentions) {
        //LOG(INFO) << "  mention " << source.span->GetText()
        //          << " item: " << store->DebugString(source.item);

        // Only consider top-level subjects for now.
        if (source.span != source.outer) continue;

        // Get facts for mention.
        if (!source.item.IsGlobalRef()) continue;
        Facts facts(&catalog_);
        facts.set_numeric_dates(true);
        facts.Extract(source.item);

        // Try to find mentions of the fact targets.
        for (int i = 0; i < facts.size(); ++i) {
          // Only search for simple facts for now.
          if (!facts.simple(i)) continue;

          // Check if the fact target is mentioned in sentence.
          Handle value = facts.last(i);
          if (targets.count(value) == 0) continue;

          // Find closest mention of fact target.
          Mention *target = nullptr;
          for (Mention &t : mentions) {
            if (t.item != value) continue;

            // Source and target should not be in the same top-level span. These
            // relations are handled by the phrase annotator.
            if (t.outer == source.outer) continue;

            // Select target with the smallest distance to the source mention.
            if (target == nullptr) {
              target = &t;
            } else {
              int current_distance = Distance(source.span, target->span);
              int new_distance = Distance(source.span, t.span);
              if (new_distance < current_distance) {
                target = &t;
              }
            }
          }
          if (target == nullptr) continue;

          // Ignore self-relations.
          if (target->item == source.item) continue;

          Handle property = facts.first(i);
          Frame prop(store, property);
          LOG(INFO) << ">>>>> '" << source.span->GetText() << "' ["
                    << store->DebugString(source.item) << "] "
                    << prop.Id() << " (" << prop.GetText("name") << ") '"
                    <<  target->span->GetText() << "' ["
                    << store->DebugString(target->item) << "]"
                    << " dist=" << Distance(source.span, target->span)
                    << (target->span != target->outer ? " nested" : "");
        }
      }
    }
  }

 private:
  // Compute distance between two spans. It is assumed that the spans are not
  // overlapping.
  static int Distance(const Span *s1, const Span *s2) {
    if (s1->begin() < s2->begin()) {
      return s2->begin() - s1->end();
    } else {
      return s1->begin() - s2->end();
    }
  }

  // Entity mention in sentence.
  struct Mention {
    Handle frame;     // frame annotations for entity
    Handle item;      // item describing entity
    Span *span;       // Span evoking frame
    Span *outer;      // Top-most containing span
  };

  // Fact catalog for fact extraction.
  FactCatalog catalog_;
};

REGISTER_ANNOTATOR("relations", RelationAnnotator);

}  // namespace nlp
}  // namespace sling


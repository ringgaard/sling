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

#include "sling/frame/serialization.h"
#include "sling/nlp/document/annotator.h"
#include "sling/nlp/document/document.h"
#include "sling/nlp/kb/facts.h"
#include "sling/nlp/kb/phrase-table.h"
#include "sling/nlp/ner/chart.h"
#include "sling/util/fingerprint.h"
#include "sling/util/mutex.h"

namespace sling {
namespace nlp {

using namespace task;

// Annotate resolved mentions with internal structure using the knowledge base
// and alias table to identify sub-mentions that are related to the frame(s)
// evoked by the mention.
class PhraseStructureAnnotator : public Annotator {
 public:
  void Init(Task *task, Store *commons) override {
    // Load phrase table.
    aliases_.Load(commons, task->GetInputFile("aliases"));

    // Initialize fact extractor.
    catalog_.Init(commons);

    // Initialize phrase cache.
    cache_size_ = task->Get("phrase_cache_size", 1024 * 1024);
    cache_.resize(cache_size_);
  }

  // Annotate multi-word expressions in document with phrase structures.
  void Annotate(Document *document) override {
    // Find all resolved multi-word expressions.
    Store *store = document->store();
    for (Span *span : document->spans()) {
      if (span->length() < 2) continue;
      Handle frame = span->evoked();
      if (frame.IsNil()) continue;

      // Get resolved item id for evoked frame.
      Handle entity = store->Resolve(frame);
      string id = store->FrameId(entity).str();
      if (id.empty()) continue;

      // Look up phrase in cache.
      string annotations;
      if (LookupPhrase(id, span->GetText(), &annotations)) {
        // Add cached phrase annotations.
        if (!annotations.empty()) {
          // Decode cached phrase annotations.
          Frame top = Decode(store, annotations).AsFrame();
          Document phrase(top, document->names());

          // Add phrase annotations to document.
          Merge(document, phrase, span->begin());
        }
      } else {
        // Get sub document with phrase span.
        Document phrase(*document, span->begin(), span->end(), false);

        // Analyze phrase structure of span.
        if (AnalyzePhrase(id, &phrase)) {
          // Add phrase annotations to document.
          Merge(document, phrase, span->begin());
        }
      }
    }
  }

  // Analyze phrase structure. Return false if there are no phrase structure
  // annotations.
  bool AnalyzePhrase(const string &id, Document *phrase) {
    // Get facts for entity.
    Store *store = phrase->store();
    Handle item = store->LookupExisting(id);
    if (item.IsNil()) return false;
    Facts facts(&catalog_);
    facts.Extract(item);

    // Create chart for finding sub-phrases.
    int length = phrase->num_tokens();
    SpanChart chart(phrase, 0, length, length);

    return false;
  }

  // Look up phrase in phrase annotation cache. Return true if the phrase is
  // found.
  bool LookupPhrase(const string &id, const string &text, string *annotations) {
    MutexLock lock(&mu_);
    Phrase &phrase = cache_[Hash(id, text) % cache_size_];
    if (id != phrase.id || text != phrase.text) return false;
    *annotations = phrase.annotations;
    return true;
  }

  // Add phrase annotations for entity alias to cache.
  void CachePhrase(const string &id, const string &text,
                   const string &annotations) {
    MutexLock lock(&mu_);
    Phrase &phrase = cache_[Hash(id, text) % cache_size_];
    phrase.id = id;
    phrase.text = text;
    phrase.annotations = annotations;
  }

  // Compute hash for id and phrase text.
  static uint32 Hash(const string &id, const string &text) {
    uint32 fp1 = Fingerprint32(id.data(), id.size());
    uint32 fp2 = Fingerprint32(text.data(), text.size());
    return fp1 ^ fp2;
  }

  // Merge annotations for phrase into document at position.
  static void Merge(Document *document, const Document &phrase, int pos) {
    int length = phrase.num_tokens();
    CHECK_GE(document->num_tokens(), pos + length);
    for (Span *span : phrase.spans()) {
      // Add new span to document (or get an existing span).
      Span *docspan = document->AddSpan(span->begin() + pos, span->end() + pos);

      // Get frame evoked from phrase span.
      Handle evoked = span->evoked();
      if (evoked.IsNil()) continue;

      // Import or merge evoked frame from phrase into document.
      Frame existing = docspan->Evoked();
      if (existing.IsNil()) {
        // Import evoked frame from phrase.
        docspan->Evoke(evoked);
      } else if (existing.IsPublic()) {
        // Replace existing frame.
        docspan->Replace(existing.handle(), evoked);
      } else {
        // Merge existing frame with phrase frame.
        Builder b(existing);
        b.AddFrom(evoked);
        b.Update();
      }
    }
  }

 private:
  // Phrase with name structure annotations in LEX format.
  struct Phrase {
    string id;             // entity id for phrase name
    string text;           // phrase text
    string annotations;    // phrase annotations as encoded SLING frames
  };

  // Phrase table with aliases.
  PhraseTable aliases_;

  // Fact catalog for fact extraction.
  FactCatalog catalog_;

  // Phrase annotation cache.
  std::vector<Phrase> cache_;
  uint32 cache_size_;

  // Mutex for accessing cache.
  Mutex mu_;
};

REGISTER_ANNOTATOR("phrase-structure", PhraseStructureAnnotator);

}  // namespace nlp
}  // namespace sling


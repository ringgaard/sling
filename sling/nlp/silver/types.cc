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

// Annotate entity types for resolved frames.
class TypeAnnotator : public Annotator {
 public:
  ~TypeAnnotator() { delete taxonomy_; }

  void Init(Task *task, Store *commons) override {
    catalog_.Init(commons);
    taxonomy_ = catalog_.CreateEntityTaxonomy();
  }

  // Annotate types for all evoked frames in document.
  bool Annotate(Document *document) override {
    Store *store = document->store();
    Handles evoked(store);
    for (Span *span : document->spans()) {
      span->AllEvoked(&evoked);
      for (Handle h : evoked) {
        Handle resolved = store->Resolve(h);
        if (resolved == h) continue;
        if (!store->IsFrame(resolved)) continue;

        Frame f(store, resolved);
        Handle type = taxonomy_->Classify(f);
        if (type.IsNil()) continue;

        Builder(store, h).AddIsA(type).Update();
      }
    }

    return true;
  }

 private:
  // Fact catalog for fact extraction.
  FactCatalog catalog_;

  // Entity type taxonomy.
  Taxonomy *taxonomy_ = nullptr;
};

REGISTER_ANNOTATOR("types", TypeAnnotator);

// Document annotator for deleting references to other frames (i.e. is: slots).
// This also removes value qualifiers for quantity and geo annotations.
class ClearReferencesAnnotator : public Annotator {
 public:
  void Init(task::Task *task, Store *commons) override {
    names_.Bind(commons);
  }

  bool Annotate(Document *document) override {
    Store *store = document->store();
    Handles evoked(store);
    for (Span *span : document->spans()) {
      span->AllEvoked(&evoked);
      for (Handle h : evoked) {
        Builder b(store, h);
        b.Delete(Handle::is());
        b.Delete(n_amount_);
        b.Delete(n_unit_);
        b.Delete(n_lat_);
        b.Delete(n_lng_);
        b.Update();
      }
    }

    return true;
  }

 private:
  Names names_;
  Name n_amount_{names_, "/w/amount"};
  Name n_unit_{names_, "/w/unit"};
  Name n_lat_{names_, "/w/lat"};
  Name n_lng_{names_, "/w/lng"};
};

REGISTER_ANNOTATOR("clear-references", ClearReferencesAnnotator);

}  // namespace nlp
}  // namespace sling


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

#include "sling/nlp/document/annotator.h"
#include "sling/nlp/document/document.h"
#include "sling/nlp/kb/facts.h"
#include "sling/string/text.h"

namespace sling {
namespace nlp {

using namespace task;

// Document annotator for filering document based in item type.
class DocumentItemFilter : public Annotator {
 public:
  ~DocumentItemFilter() { delete taxonomy_; }

  void Init(Task *task, Store *commons) override {
    CHECK(names_.Bind(commons));
    string type_list = task->Get("item_types", "Q5");
    auto types = Text(type_list).split(',');
    catalog_.Init(commons);
    taxonomy_ = new Taxonomy(&catalog_, types);
  }

  bool Annotate(Document *document) override {
    Frame item = document->top().GetFrame(n_page_item_);
    if (!item.valid()) return false;
    if (taxonomy_->Classify(item).IsNil()) return false;
    return true;
  }

 private:
  // Fact catalog for fact extraction.
  FactCatalog catalog_;

  // Entity type taxonomy.
  Taxonomy *taxonomy_ = nullptr;

  // Symbols.
  Names names_;
  Name n_page_item_{names_, "/wp/page/item"};
};

REGISTER_ANNOTATOR("document-item-filter", DocumentItemFilter);

// Discard all except the first section of document.
class DocumentPrologueAnnotator : public Annotator {
 public:
  bool Annotate(Document *document) override {
    // Skip empty documents.
    if (document->length() == 0) return false;

    // Extract first section as prologue.
    int end = 1;
    while (end < document->length()) {
      if (document->token(end).style() & HEADING_BEGIN) break;
      end++;
    }

    if (end < document->length()) {
      Document prologue(*document, 0, end, true);
      prologue.RemoveThemes();
      document->Swap(&prologue);
    }

    return true;
  }
};

REGISTER_ANNOTATOR("document-prologue", DocumentPrologueAnnotator);


}  // namespace nlp
}  // namespace sling


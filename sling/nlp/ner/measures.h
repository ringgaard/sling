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

#ifndef SLING_NLP_NER_MEASURES_H_
#define SLING_NLP_NER_MEASURES_H_

#include "sling/base/types.h"
#include "sling/frame/store.h"
#include "sling/nlp/document/document.h"
#include "sling/nlp/kb/facts.h"
#include "sling/nlp/kb/phrase-table.h"
#include "sling/nlp/ner/chart.h"

namespace sling {
namespace nlp {

class SpanTaxonomy {
 public:
  ~SpanTaxonomy();
  void Init(Store *store);
  void Annotate(const PhraseTable &aliases, SpanChart *chart);
 private:
  FactCatalog catalog_;
  Taxonomy *taxonomy_ = nullptr;
};

class NumberAnnotator {
 public:
  enum Format {STANDARD, IMPERIAL, NORWEGIAN};

  void Init(Store *store);
  void Annotate(SpanChart *chart);
 private:
  static Handle ParseNumber(Text str, char tsep, char dsep, char msep);
  static Handle ParseNumber(Text str, Format format);

  Names names_;
  Name n_natural_number_{names_, "Q21199"};
  Name n_lang_{names_, "lang"};
  Name n_english_{names_, "/lang/en"};
  Name n_time_{names_, "/w/time"};
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_NER_CHART_H_

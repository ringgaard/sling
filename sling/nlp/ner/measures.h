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

class SpanAnnotator {
 public:
  void Init(Store *store);

  Handle FindMatch(const PhraseTable &aliases,
                   const PhraseTable::Phrase *phrase,
                   const Name &type,
                   Store *store);

 protected:
  Names names_;
  Name n_instance_of_{names_, "P31"};
};

class SpanTaxonomy : public SpanAnnotator {
 public:
  ~SpanTaxonomy();
  void Init(Store *store);
  void Annotate(const PhraseTable &aliases, SpanChart *chart);
 private:
  FactCatalog catalog_;
  Taxonomy *taxonomy_ = nullptr;
  HandleMap<int> type_flags_;
};

class NumberAnnotator : public SpanAnnotator {
 public:
  enum Format {STANDARD, IMPERIAL, NORWEGIAN};

  void Annotate(SpanChart *chart);
 private:
  static Handle ParseNumber(Text str, char tsep, char dsep, char msep);
  static Handle ParseNumber(Text str, Format format);

  Name n_natural_number_{names_, "Q21199"};
  Name n_lang_{names_, "lang"};
  Name n_english_{names_, "/lang/en"};
  Name n_time_{names_, "/w/time"};
};

class MeasureAnnotator : public SpanAnnotator {
 public:
  void Init(Store *store);
  void Annotate(const PhraseTable &aliases, SpanChart *chart);
 private:
  void AddQuantity(SpanChart *chart, int begin, int end,
                   Handle amount, Handle unit);

  HandleSet units_;
  Name n_quantity_{names_, "/w/quantity"};
  Name n_amount_{names_, "/w/amount"};
  Name n_unit_{names_, "/w/unit"};
};

class DateAnnotator : public SpanAnnotator {
 public:
  void Init(Store *store);
  void Annotate(const PhraseTable &aliases, SpanChart *chart);

 private:
  int GetYear(const PhraseTable &aliases, Store *store, SpanChart *chart,
              int pos, int *end);

  void AddDate(SpanChart *chart, int begin, int end, const Date &date);

  Calendar calendar_;

  Name n_point_in_time_{names_, "P585"};
  Name n_time_{names_, "/w/time"};
  Name n_calendar_day_{names_, "Q47150325"};
  Name n_calendar_month_{names_, "Q47018478"};
  Name n_day_of_year_{names_, "Q14795564"};
  Name n_month_{names_, "Q47018901"};
  Name n_year_{names_, "Q577"};
  Name n_year_bc_{names_, "Q29964144"};
  Name n_decade_{names_, "Q39911"};
  Name n_century_{names_, "Q578"};
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_NER_CHART_H_

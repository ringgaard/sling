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

// Span categorization flags.
enum SpanFlags {
  SPAN_NUMBER            = (1 << 0),
  SPAN_NATURAL_NUMBER    = (1 << 1),
  SPAN_UNIT              = (1 << 2),
  SPAN_CURRENCY          = (1 << 3),
  SPAN_YEAR              = (1 << 4),
  SPAN_YEAR_BC           = (1 << 5),
  SPAN_MONTH             = (1 << 6),
  SPAN_WEEKDAY           = (1 << 7),
  SPAN_CALENDAR_MONTH    = (1 << 8),
  SPAN_CALENDAR_DAY      = (1 << 9),
  SPAN_DAY_OF_YEAR       = (1 << 10),
  SPAN_DECADE            = (1 << 11),
  SPAN_CENTURY           = (1 << 12),
  SPAN_DATE              = (1 << 13),
  SPAN_MEASURE           = (1 << 14),
  SPAN_GEO               = (1 << 15),

  SPAN_FAMILY_NAME       = (1 << 16),
  SPAN_GIVEN_NAME        = (1 << 17),

  SPAN_PERSON            = (1 << 18),
  SPAN_LOCATION          = (1 << 19),
  SPAN_ORGANIZATION      = (1 << 20),
};

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

// Populate chart with phrase matches.
class SpanPopulator : public SpanAnnotator {
 public:
  void Annotate(const PhraseTable &aliases, SpanChart *chart);

  // Add stop word.
  void AddStopWord(Text word);

 private:
  // Check if token is a stop word.
  bool Discard(const Token &token) const;

  // Fingerprints for stop words.
  std::unordered_set<uint64> fingerprints_;
};

class SpanImporter : public SpanAnnotator {
 public:
  void Annotate(const PhraseTable &aliases, SpanChart *chart);
 private:
  Name n_time_{names_, "/w/time"};
  Name n_quantity_{names_, "/w/quantity"};
  Name n_geo_{names_, "/w/geo"};
};

class SpanTaxonomy : public SpanAnnotator {
 public:
  ~SpanTaxonomy();
  void Init(Store *store);
  void Annotate(const PhraseTable &aliases, SpanChart *chart);
 private:
  int Classify(const Frame &item);

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

class NumberScaleAnnotator : public SpanAnnotator {
 public:
  void Init(Store *store);
  void Annotate(const PhraseTable &aliases, SpanChart *chart);
 private:
  HandleMap<float> scalars_;
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

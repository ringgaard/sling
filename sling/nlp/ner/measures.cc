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

#include "sling/nlp/ner/measures.h"

#include "sling/frame/object.h"
#include "sling/nlp/kb/calendar.h"

namespace sling {
namespace nlp {

// Measures:
//  number (float/integer) and compound numbers (e.g. 15 mio)
//  date plus stand-alone years (1000-2100), month and year, stand-alone month, and weekdays
//  quantity with unit
//  amount with currency
//  entities (person, location, organization, facility)
//
// Add all anchors from input document that matches in the phrase tables and add the
// correct resolution as the aux item.
//
// For persons, add last name mentions as resolved mentions
// Aux items take precendence over the matches
//
// Absolute calendar types:
//   millennium (Q36507)
//   century (Q578)
//   decade (Q39911)
//   year (Q577)
//   calendar day of a given year (Q47150325) (e.g. 3 February 2015)
//   calendar month of a given year (Q47018478) (e.g February 2015)
//
// Relative calendar types:
//   calendar month (Q47018901) (January, February, ...)
//   determinator for date of periodic occurrence (Q14795564) (e.g. February 3)
//   day of the week (Q41825) (Monday, Tueday, ...)
//   day of the week within a given month (Q51118183) (e.g. second Tuesday in May)
//
// integer number between 1592 and 2038 is year if it is only digits

SpanTaxonomy::~SpanTaxonomy() {
  delete taxonomy_;
}

void SpanTaxonomy::Init(Store *store) {
#if 0
  static const char *span_taxonomy[] = {
    "Q215627",     // person
    "Q4164871",    // position
    "Q12737077",   // occupation
    "Q216353",     // title
    "Q4438121",    // sports organization
    "Q215380",     // band
    "Q2385804",    // educational institution
    "Q783794",     // company
    "Q17334923",   // location
    "Q43229",      // organization
    "Q2188189",    // musical work
    "Q571",        // book
    "Q732577",     // publication
    "Q11424",      // film
    "Q101352",     // family name
    "Q202444",     // given name
    "Q838948",     // work of art
    "Q47461344",   // written work

    "Q47150325",   // calendar day of a given year
    "Q47018478",   // calendar month of a given year
    "Q14795564",   // determinator for date of periodic occurrence
    "Q41825",      // day of the week
    "Q47018901",   // calendar month
    "Q577",        // year
    "Q21199",      // natural number
    "Q66055",      // fraction
    "Q47574",      // unit of measurement
    nullptr,
  };
#endif

  static std::pair<const char *, int> span_taxonomy[] = {
    {"Q47150325", SPAN_CALENDAR_DAY},     // calendar day of a given year
    {"Q47018478", SPAN_CALENDAR_MONTH},   // calendar month of a given year
    {"Q14795564", SPAN_CALENDAR_PERIOD},  // date of periodic occurrence
    {"Q41825",    SPAN_WEEKDAY},          // day of the week
    {"Q47018901", SPAN_MONTH},            // calendar month
    {"Q577",      SPAN_YEAR},             // year
    {"Q29964144", SPAN_YEAR},             // year BC
    {"Q21199",    SPAN_NUMBER},           // natural number
    {"Q66055",    SPAN_FRACTION},         // fraction
    {"Q47574",    SPAN_UNIT},             // unit of measurement
    {nullptr, 0},
  };

  std::vector<Text> types;
  for (auto *type = span_taxonomy; type->first != nullptr; ++type) {
    const char *name = type->first;
    int flags = type->second;
    Handle t = store->LookupExisting(name);
    if (t.IsNil()) {
      LOG(WARNING) << "Ignoring unknown type in taxonomy: " << name;
      continue;
    }
    type_flags_[t] = flags;
    types.push_back(name);
  }

  catalog_.Init(store);
  taxonomy_ = new Taxonomy(&catalog_, types);
}

void SpanTaxonomy::Annotate(const PhraseTable &aliases, SpanChart *chart) {
  Document *document = chart->document();
  Store *store = document->store();
  PhraseTable::MatchList matches;
  Handle name = store->Lookup("name");
  for (int b = 0; b < chart->size(); ++b) {
    int end = std::min(b + chart->maxlen(), chart->size());
    for (int e = b + 1; e <= end; ++e) {
      CaseForm form = document->Form(b, e);
      //LOG(INFO) << b << "-" << e << " form: " << form;
      SpanChart::Item &span = chart->item(b, e);
      aliases.GetMatches(span.matches, &matches);
      for (const auto &match : matches) {
        // Skip if case forms conflict.
        Frame item(store, match.item);
        if (match.form != CASE_NONE &&
            form != CASE_NONE &&
            match.form != form) {
          //LOG(INFO) << "Skip '" << document->PhraseText(b + chart->begin(), e + chart->begin()) << "': " << item.Id() << " " << item.GetString(name);
          continue;
        }

        Handle type = taxonomy_->Classify(item);
        if (type.IsNil()) continue;
        Frame t(store, type);

        auto f = type_flags_.find(type);
        if (f != type_flags_.end()) {
          span.flags |= f->second;
          LOG(INFO) << "'" << document->PhraseText(b + chart->begin(), e + chart->begin()) << "': " << item.Id() << " " << item.GetString(name) << " is " << t.GetString(name);
        }
      }
    }
  }
}

void NumberAnnotator::Init(Store *store) {
  CHECK(names_.Bind(store));
}

void NumberAnnotator::Annotate(SpanChart *chart) {
  // Get document language.
  Document *document = chart->document();
  Handle lang = document->top().GetHandle(n_lang_);
  if (lang.IsNil()) lang = n_english_.handle();
  Format format = lang == n_english_ ? IMPERIAL : STANDARD;

  for (int t = chart->begin(); t < chart->end(); ++t) {
    const string &word = document->token(t).word();

    // Check if token contains digits.
    bool has_digits = false;
    bool all_digits = true;
    for (char c : word) {
      if (c >= '0' && c <= '9') {
        has_digits = true;
      } else {
        all_digits = true;
      }
    }

    // Try to parse token as a number.
    if (has_digits) {
      Handle number = ParseNumber(word, format);
      if (!number.IsNil()) {
        // Numbers between 1582 and 2038 are considered years.
        int flags = SPAN_NUMBER;
        if (word.size() == 4 && all_digits && number.IsInt()) {
          int value = number.AsInt();
          if (value >= 1582 && value <= 2038) {
            Builder b(document->store());
            b.AddIsA(n_time_);
            b.AddIs(number);
            number = b.Create().handle();
            flags = SPAN_DATE;
          }
        }
        chart->Add(t, t + 1, number, flags);
      }
    }
  }
}

Handle NumberAnnotator::ParseNumber(Text str, char tsep, char dsep, char msep) {
  const char *p = str.data();
  const char *end = p + str.size();
  if (p == end) return Handle::nil();

  // Parse sign.
  double scale = 1.0;
  if (*p == '-') {
    scale = -1.0;
    p++;
  } else if (*p == '+') {
    p++;
  }

  // Parse integer part.
  double value = 0.0;
  const char *group = nullptr;
  while (p < end) {
    if (*p >= '0' && *p <= '9') {
      value = value * 10.0 + (*p++ - '0');
    } else if (*p == tsep) {
      if (group != nullptr && p - group != 3) return Handle::nil();
      group = p;
      p++;
    } else if (*p == dsep) {
      break;
    } else {
      return Handle::nil();
    }
  }
  if (group != nullptr && p - group != 3) return Handle::nil();

  // Parse decimal part.
  bool decimal = false;
  if (p < end && *p == dsep) {
    decimal = true;
    p++;
    group = nullptr;
    while (p < end) {
      if (*p >= '0' && *p <= '9') {
        value = value * 10.0 + (*p++ - '0');
        scale /= 10.0;
      } else if (*p == msep) {
        if (group != nullptr && p - group != 3) return Handle::nil();
        group = p;
        p++;
      } else {
        return Handle::nil();
      }
    }
    if (group != nullptr && p - group != 3) return Handle::nil();
  }
  if (p != end) return Handle::nil();

  // Compute number.
  value *= scale;
  if (decimal || value < Handle::kMinInt || value > Handle::kMaxInt) {
    return Handle::Float(value);
  } else {
    return Handle::Integer(value);
  }
}

Handle NumberAnnotator::ParseNumber(Text str, Format format) {
  Handle number = Handle::nil();
  switch (format) {
    case STANDARD:
      number = ParseNumber(str, '.', ',', 0);
      if (number.IsNil()) number = ParseNumber(str, ',', '.', 0);
      break;
    case IMPERIAL:
      number = ParseNumber(str, ',', '.', 0);
      if (number.IsNil()) number = ParseNumber(str, '.', ',', 0);
      break;
    case NORWEGIAN:
      number = ParseNumber(str, ' ', '.', ' ');
      if (number.IsNil()) number = ParseNumber(str, '.', ',', 0);
      break;
  }
  return number;
}

void DateAnnotator::Init(Store *store) {
  CHECK(names_.Bind(store));
}

Handle DateAnnotator::FindMatchByType(const PhraseTable &aliases,
                                      const PhraseTable::Phrase *phrase,
                                      Handle type,
                                      Store *store) {
  Handles matches(store);
  aliases.GetMatches(phrase, &matches);
  for (Handle h : matches) {
    Frame item(store, h);
    for (const Slot &s : item) {
      if (s.name == n_instance_of_ && store->Resolve(s.value) == type) {
        return h;
      }
    }
  }
  return Handle::nil();
}

void DateAnnotator::AddDate(SpanChart *chart, int begin, int end,
                            const Date &date) {
  Store *store = chart->document()->store();
  Builder builder(store);
  builder.AddIsA(n_time_);
  builder.AddIs(date.AsHandle(store));
  Handle h = builder.Create().handle();
  chart->Add(begin + chart->begin(), end + chart->begin(), h, SPAN_DATE);
}

void DateAnnotator::Annotate(const PhraseTable &aliases, SpanChart *chart) {
  Document *document = chart->document();
  Store *store = document->store();
  PhraseTable::MatchList matches;
  for (int b = 0; b < chart->size(); ++b) {
    int end = std::min(b + chart->maxlen(), chart->size());
    for (int e = end; e > b; --e) {
      SpanChart::Item &span = chart->item(b, e);
      Date date;
      if (span.flags & SPAN_CALENDAR_DAY) {
        // Date with year, month and day.
        LOG(INFO) << "Calendar date: " << document->PhraseText(b + chart->begin(), e + chart->begin());
        Handle h = FindMatchByType(aliases, span.matches,
                                   n_calendar_day_.handle(), store);
        if (!h.IsNil()) {
          Frame item(store, h);
          date.ParseFromFrame(item);
          if (date.precision == Date::DAY) {
            AddDate(chart, b, e, date);
            b = e;
            break;
          }
        }
      } else if (span.flags & SPAN_CALENDAR_MONTH) {
        LOG(INFO) << "Calendar month: " << document->PhraseText(b + chart->begin(), e + chart->begin());
        b = e;
        break;
      } else if (span.flags & SPAN_CALENDAR_PERIOD) {
        LOG(INFO) << "Period: " << document->PhraseText(b + chart->begin(), e + chart->begin());
        b = e;
        // TODO: get year
        break;
      } else if (span.flags & SPAN_CALENDAR_MONTH) {
        LOG(INFO) << "Month: " << document->PhraseText(b + chart->begin(), e + chart->begin());
        b = e;
        // TODO: get year
        break;
      } else if (span.flags & SPAN_YEAR) {
        LOG(INFO) << "Year: " << document->PhraseText(b + chart->begin(), e + chart->begin());
        b = e;
        break;
      }
    }
  }
}

}  // namespace nlp
}  // namespace sling


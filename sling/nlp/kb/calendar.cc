#include "sling/nlp/kb/calendar.h"

#include "sling/base/logging.h"
#include "sling/frame/object.h"
#include "sling/frame/store.h"
#include "sling/string/strcat.h"
#include "sling/string/text.h"

namespace sling {
namespace nlp {

static const char *parse_number(const char *p, const char *end, int *value) {
  int n = 0;
  while (p < end && *p >= '0' && *p <= '9') {
    n = n * 10 + (*p++ - '0');
  }
  *value = n;
  return p;
}

void Calendar::Init(Store *store) {
  // Get symbols.
  store_ = store;
  n_name_ = store->Lookup("name");

  // Get calendar from store.
  Frame cal(store, "/w/calendar");
  if (!cal.valid()) return;

  // Get weekdays.
  for (const Slot &s : cal.GetFrame("/w/weekdays")) {
    weekdays_[s.name.AsInt()] = s.value;
  }

  // Get months.
  for (const Slot &s : cal.GetFrame("/w/months")) {
    months_[s.name.AsInt()] = s.value;
  }

  // Get days.
  for (const Slot &s : cal.GetFrame("/w/days")) {
    days_[s.name.AsInt()] = s.value;
  }

  // Get decades.
  for (const Slot &s : cal.GetFrame("/w/decades")) {
    decades_[s.name.AsInt()] = s.value;
  }

  // Get centuries.
  for (const Slot &s : cal.GetFrame("/w/centuries")) {
    centuries_[s.name.AsInt()] = s.value;
  }

  // Get millennia.
  for (const Slot &s : cal.GetFrame("/w/millennia")) {
    millennia_[s.name.AsInt()] = s.value;
  }
};

string Calendar::DateAsString(const Object &date) const {
  // Parse date.
  Date d(date);
  Text year = YearName(d.year());

  if (d.day() == 0) {
    if (d.month() == 0) {
      // Date with year precision.
      if (!year.empty()) {
        return year.str();
      } else {
        return StrCat(d.year());
      }
    } else {
      // Date with month precision.
      Text month = MonthName(d.month());
      if (!month.empty()) {
        if (!year.empty()) {
          return StrCat(month, " ", year);
        } else {
          return StrCat(month, " ", d.year());
        }
      } else {
        // Fall back to Y/M format.
        return StrCat(d.year(), "/", d.month());
      }
    }
  } else {
    // Date with year, month, and day.
    Text day = DayName(d.month(), d.day());
    if (!day.empty()) {
      if (!year.empty()) {
        return StrCat(day, ", ", year);
      } else {
        return StrCat(day, ", ", d.year());
      }
    } else {
      // Fall back to Y/M/D format.
      return StrCat(d.year(), "/", d.month(), "/", d.day());
    }
  }
}

Handle Calendar::Day(int month, int day) const {
  auto f = days_.find(month * 100 + day);
  return f != days_.end() ? f->second : Handle::nil();
}

Handle Calendar::Month(int month) const {
  auto f = months_.find(month);
  return f != months_.end() ? f->second : Handle::nil();
}

Handle Calendar::Year(int year) const {
  auto f = years_.find(year);
  return f != years_.end() ? f->second : Handle::nil();
}

Text Calendar::ItemName(Handle item) const {
  if (!store_->IsFrame(item)) return "";
  FrameDatum *frame = store_->GetFrame(item);
  Handle name = frame->get(n_name_);
  if (!store_->IsString(name)) return "";
  StringDatum *str = store_->GetString(name);
  return str->str();
}

Date::Date(const Object &object) {
  year_ = month_ = day_ = 0;
  if (object.IsInt()) {
    int num = object.AsInt();
    CHECK(num > 0);
    if (num < 10000) {
      // YYYY
      year_ = num;
    } else if (num < 1000000) {
      // YYYYMM
      year_ = num / 100;
      month_ = num % 100;
    } else {
      // YYYYMMDD
      year_ = num / 10000;
      month_ = (num % 10000) / 100;
      day_ = num % 100;
    }
  } else if (object.IsString()) {
    // Parse string date format: [+-]YYYY-MM-DDT00:00:00Z.
    Text datestr = object.AsString().text();
    const char *p = datestr.data();
    const char *end = p + datestr.size();
    bool bc = false;
    if (p < end && *p == '+') {
      p++;
    } else if (p < end && *p == '-') {
      bc = true;
      p++;
    }
    p = parse_number(p, end, &year_);
    if (bc) year_ = -year_;
    if (p < end && *p == '-') {
      p++;
      p = parse_number(p, end, &month_);
      if (p < end && *p == '-') {
        p++;
        p = parse_number(p, end, &day_);
      }
    }
  }
}

}  // namespace nlp
}  // namespace sling


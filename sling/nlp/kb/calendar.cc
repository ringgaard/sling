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

  // Get years.
  for (const Slot &s : cal.GetFrame("/w/years")) {
    years_[s.name.AsInt()] = s.value;
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

  switch (d.precision()) {
    case Date::MILLENNIUM: {
      Text millennium = MillenniumName(d.year());
      if (!millennium.empty()) {
        return millennium.str();
      } else if (d.year() > 0) {
        return StrCat((d.year() - 1) / 1000 + 1, ". millennium AD");
      } else {
        return StrCat(-((d.year() + 1) / 1000 - 1), ". millennium BC");
      }
    }

    case Date::CENTURY: {
      Text century = CenturyName(d.year());
      if (!century.empty()) {
        return century.str();
      } else if (d.year() > 0) {
        return StrCat((d.year() - 1) / 100 + 1, ". century AD");
      } else {
        return StrCat(-((d.year() + 1) / 100 - 1), ". century BC");
      }
    }

    case Date::DECADE: {
      Text decade = DecadeName(d.year());
      if (!decade.empty()) {
        return decade.str();
      } else {
        return StrCat(year, "s");
      }
    }

    case Date::YEAR: {
      if (!year.empty()) {
        return year.str();
      } else if (d.year() > 0) {
        return StrCat(d.year());
      } else {
        return StrCat(-d.year(), " BC");
      }
    }

    case Date::MONTH: {
      Text month = MonthName(d.month());
      if (!month.empty()) {
        if (!year.empty()) {
          return StrCat(month, " ", year);
        } else {
          return StrCat(month, " ", d.year());
        }
      } else {
        return StrCat(d.year(), "-", d.month());
      }
    }

    case Date::DAY: {
      Text day = DayName(d.month(), d.day());
      if (!day.empty()) {
        if (!year.empty()) {
          return StrCat(day, ", ", year);
        } else {
          return StrCat(day, ", ", d.year());
        }
      } else {
        return StrCat(d.year(), "-", d.month(), "-", d.day());
      }
    }
  }

  return "???";
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

Handle Calendar::Decade(int year) const {
  int decade = year / 10;
  if (year < 0) decade--;
  auto f = decades_.find(decade);
  return f != decades_.end() ? f->second : Handle::nil();
}

Handle Calendar::Century(int year) const {
  int century = year > 0 ? (year - 1) / 100 + 1 : (year + 1) / 100 - 1;
  auto f = centuries_.find(century);
  return f != centuries_.end() ? f->second : Handle::nil();
}

Handle Calendar::Millennium(int year) const {
  int millennium = year > 0 ? (year - 1) / 1000 + 1 : (year + 1) / 1000 - 1;
  auto f = millennia_.find(millennium);
  return f != millennia_.end() ? f->second : Handle::nil();
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
  precision_ = DAY;
  if (object.IsInt()) {
    int num = object.AsInt();
    CHECK(num > 0);
    if (num >= 1000000) {
      // YYYYMMDD
      year_ = num / 10000;
      month_ = (num % 10000) / 100;
      day_ = num % 100;
      precision_ = DAY;
    } else if (num >= 10000) {
      // YYYYMM
      year_ = num / 100;
      month_ = num % 100;
      precision_ = MONTH;
    } else if (num >= 1000) {
      // YYYY
      year_ = num;
      precision_ = YEAR;
    } else if (num >= 100) {
      // YYY*
      year_ = num * 10;
      precision_ = DECADE;
    } else if (num >= 10) {
      // YY**
      year_ = num * 100;
      precision_ = CENTURY;
    } else {
      // Y***
      year_ = num * 1000;
      precision_ = MILLENNIUM;
    }
  } else if (object.IsString()) {
    // Parse string date format: [+-]YYYY-MM-DDT00:00:00Z.
    Text datestr = object.AsString().text();
    const char *p = datestr.data();
    const char *end = p + datestr.size();

    // Parse + and - for AD and BC.
    bool bc = false;
    if (p < end && *p == '+') {
      p++;
    } else if (p < end && *p == '-') {
      bc = true;
      p++;
    }

    // Parse year, which can have trailing *s to indicate precision.
    p = parse_number(p, end, &year_);
    int stars = 0;
    while (p < end && *p == '*') {
      p++;
      stars++;
    }
    switch (stars) {
      case 0: precision_ = YEAR; break;
      case 1: precision_ = DECADE; break;
      case 2: precision_ = CENTURY; break;
      case 3: precision_ = MILLENNIUM; break;
    }
    if (bc) year_ = -year_;

    // Parse day and month.
    if (p < end && *p == '-') {
      p++;
      p = parse_number(p, end, &month_);
      if (month_ != 0) precision_ = MONTH;
      if (p < end && *p == '-') {
        p++;
        p = parse_number(p, end, &day_);
        if (day_ != 0) precision_ = DAY;
      }
    }
  }
}

}  // namespace nlp
}  // namespace sling


#ifndef SLING_NLP_KB_CALENDAR_H_
#define SLING_NLP_KB_CALENDAR_H_

#include <string>
#include <unordered_map>

#include "sling/base/types.h"
#include "sling/frame/object.h"
#include "sling/frame/store.h"
#include "sling/string/text.h"

namespace sling {
namespace nlp {

// Calendar with items about (Gregorian) calendar concepts.
class Calendar {
 public:
  // Initialize calendar from store.
  void Init(Store *store);

  // Convert date to string.
  string DateAsString(const Object &date) const;

  // Get item for day.
  Handle Day(int month, int day) const;
  Text DayName(int month, int day) const { return ItemName(Day(month, day)); }

  // Get item for month.
  Handle Month(int month) const;
  Text MonthName(int month) const { return ItemName(Month(month)); }

  // Get item for year.
  Handle Year(int year) const;
  Text YearName(int year) const { return ItemName(Year(year)); }

 private:
  // Get name for item.
  Text ItemName(Handle item) const;

  // Mapping from calendar item key to the corresponding calendar item.
  typedef std::unordered_map<int, Handle> CalendarMap;

  // Store with calendar.
  Store *store_ = nullptr;

  // Symbols.
  Handle n_name_;

  // Weekdays (0=Sunday).
  CalendarMap weekdays_;

  // Months (1=January).
  CalendarMap months_;

  // Days of year where the key is month*100+day.
  CalendarMap days_;

  // Years where BCE are represented by negative numbers. There is no year 0 in
  // the AD calendar although the is an item for this concept.
  CalendarMap years_;

  // Decades. The decades are numbered as year/10, e.g. the decade from
  // 1970-1979 has 197 as the key. The last decade before AD, i.e. years 1-9 BC,
  // has decade number -1. Likewise, the first decade after AD, i.e. years
  // 1-9 AD, has number 0. These two decades only has nine years.
  CalendarMap decades_;

  // Centuries. The centuries are numbered as (year-1)/100+1 for AD and
  // (year+1)/100-1 for BC, so the 19. century, from 1801 to 1900, has number
  // 19. Centuries BCE are represented by negative numbers. The 1st century BC,
  // from 100BC to 1BC, has number -1.
  CalendarMap centuries_;

  // Millennia. The millennia are numbered as (year-1)/1000+1 for AD and
  // (year+1)/1000-1 for BC.
  CalendarMap millennia_;
};

// Date with day, month, and year.
class Date {
 public:
  // Initialize date from object.
  Date(const Object &object);

  // Return year. Return 0 if date is invalid.
  int year() const { return year_; }

  // Return month (1=January). Returns 0 if no month in date.
  int month() const { return month_; }

  // Return day of month (first day of month is 1). Return 0 if no day in date.
  int day() const { return day_; }

 private:
  int year_;
  int month_;
  int day_;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_KB_CALENDAR_H_


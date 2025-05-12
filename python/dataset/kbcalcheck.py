# Copyright 2021 Ringgaard Research ApS
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http:#www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Check calendar items in knowledge base."""

import sling

kb = sling.Store()
kb.load("data/e/kb/kb.sling")

calendar = kb["/w/calendar"]

weekdays = [
  "Sunday",
  "Monday",
  "Tuesday",
  "Wednesday",
  "Thursday",
  "Friday",
  "Saturday",
]

months = [
  "January",
  "February",
  "March",
  "April",
  "May",
  "June",
  "July",
  "August",
  "September",
  "October",
  "November",
  "December",
]

def ordinal(n):
  if n % 10 == 1 and n % 100 != 11: return str(n) + "st"
  if n % 10 == 2 and n % 100 != 12: return str(n) + "nd"
  if n % 10 == 3 and n % 100 != 13: return str(n) + "rd"
  return str(n) + "th"

errors = 0

print("*** Check weekdays")
for k, v, in calendar["/w/weekdays"]:
  if str(v.name) != weekdays[k]:
    print("Weekday mismatch", v.id, v.name, weekdays[k])
    errors += 1

print("*** Check months")
for k, v, in calendar["/w/months"]:
  if str(v.name) != months[k - 1]:
    print("Month mismatch", v.id, v.name, months[k - 1])
    errors += 1

print("*** Check days")
for k, v, in calendar["/w/days"]:
  day = months[k // 100 - 1] + " " + str(k % 100)
  if str(v.name) != day:
    print("Day mismatch", v.id, v.name, day)
    errors += 1

print("*** Check years")
for k, v, in calendar["/w/years"]:
  if k > 0:
    year = str(k)
  elif k < 0:
    year = str(-k) + " BC"
  else:
    continue

  if str(v.name) != year:
    print("Year mismatch", v.id, v.name, year)
    errors += 1

  dt = sling.Date(v)
  if dt.precision != sling.YEAR or dt.year != k:
    print("Year time mismatch", v.id, v.name, year, dt, dt.year)
    errors += 1

print("*** Check decades")
for k, v, in calendar["/w/decades"]:
  if k > 0:
    year = k * 10
    decade = str(year) + "s"
  elif k < 0:
    year = (k + 1) * 10
    decade = str(-year) + "s BC"
  else:
    continue

  if str(v.name) != decade and str(v.name) != decade + "E":
    print("Decade mismatch", v.id, v.name, decade)
    errors += 1

  if k == -1: continue

  dt = sling.Date(v)
  if dt.precision != sling.DECADE or dt.year != year:
    print("Decade time mismatch", v.id, v.name, year, dt.year)
    errors += 1

print("*** Check centuries")
for k, v, in calendar["/w/centuries"]:
  if k > 0:
    year = (k - 1) * 100 + 1
    century = ordinal(k) + " century"
  elif k < 0:
    year = (k + 1) * 100 - 1
    century = ordinal(-k) + " century BC"
  else:
    continue

  if abs(year) > 9000: continue

  if str(v.name) != century and str(v.name) != century + "E":
    print("Century mismatch", v.id, v.name, century)
    errors += 1

  dt = sling.Date(v)
  if dt.precision != sling.CENTURY or dt.year != year:
    print("Century time mismatch", v.id, v.name, year, dt.year)
    errors += 1

print("*** Check millennia")
for k, v, in calendar["/w/millennia"]:
  if k > 0:
    year = (k - 1) * 1000 + 1
    millennium = ordinal(k) + " millennium"
  elif k < 0:
    year = (k + 1) * 1000 - 1
    millennium = ordinal(-k) + " millennium BC"
  else:
    continue

  if abs(year) > 9000: continue

  if str(v.name) != millennium and str(v.name) != millennium + "E":
    print("Century mismatch", v.id, v.name, millennium)
    errors += 1

  dt = sling.Date(v)
  if dt.precision != sling.MILLENNIUM or dt.year != year:
    print("Millennium time mismatch", v.id, v.name, year, dt.year)
    errors += 1

if errors > 0: raise Exception("errors in calendar")

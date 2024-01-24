# Copyright 2024 Ringgaard Research ApS
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

"""Check obituaries for matches in knowledge base."""

import json
import sling
import sling.flags as flags

flags.define("--db",
             default="vault/afdoede",
             help="obituary database")

flags.define("--strict",
             help="strict birth date matching",
             default=False,
             action="store_true")

flags.parse()

# Load KB.
print("Loading KB")
kb = sling.Store()
kb.load("data/e/kb/kb.sling")
n_id = kb["id"]
n_name = kb["name"]
n_instanceof = kb["P31"]
n_human = kb["Q5"]
n_birthdate = kb["P569"]
n_deathdate = kb["P570"]
cal = sling.Calendar(kb)
aliases = sling.PhraseTable(kb, "data/e/kb/da/phrase-table.repo")

db = sling.Database(flags.arg.db)

def get_date(item, prop):
 value = kb.resolve(item[prop])
 if value is None: return None
 return sling.Date(value)

def date_str(d):
  if d is None: return ""
  return cal.str(d)

def compartible_dates(d1, d2):
  if d1 is None: return False
  if d2 is None: return False
  if d1.year != d2.year: return False
  if d1.value() == d2.value(): return True
  if d1.precision == sling.YEAR and d1.year == d2.year: return True
  if d2.precision == sling.YEAR and d1.year == d2.year: return True
  return False

for rec in db.values():
  data = json.loads(rec)
  name = data["name"]
  dob = sling.Date(data["birth"])
  dod = sling.Date(data["death"])

  first = True
  for m in aliases.lookup(name):
    if m is None: continue
    if m[n_instanceof] != n_human: continue

    born = get_date(m, n_birthdate)
    died = get_date(m, n_deathdate)
    if born is None: continue
    if not compartible_dates(dob, born): continue
    if compartible_dates(dod, died): continue
    if died != None and died.year != dod.year: continue

    if flags.arg.strict:
      # Only precise birth match.
      if dob.precision != sling.DAY or born.precision != sling.DAY: continue

    if first:
      print(name, "(", date_str(dob), "-", date_str(dod), ")")
      print("https://afdoede.dk/minde/" + data["mindeid"])
      obutuary = data["obituary"]
      if obutuary: print(obutuary)
      first = False

    print("  match", m[n_id], m[n_name], date_str(born), "-", date_str(died))

  if not first: print("\n------------------------------------------------\n")

db.close()


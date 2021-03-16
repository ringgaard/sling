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

"""Generate list of LEI registered authorities in SLING format."""

import csv
import sling

# Download:
#   https://www.gleif.org/en/about-lei/code-lists/gleif-registration-authorities-list
#
# Fields:
# 0: Registration Authority Code
# 1: Country
# 2: Country Code
# 3: Jurisdiction (country or region)
# 4: International name of Register
# 5: Local name of Register
# 6: International name of organisation responsible for the Register
# 7: Local name of organisation responsible for the Register
# 8: Website
# 9: Date IP disclaimer
# 10: Comments
# 11: End Date

kb = sling.Store()
kb.load("data/e/kb/kb.sling")
aliases = sling.PhraseTable(kb, "data/e/kb/en/phrase-table.repo")

def resolve_name(name):
  for item in aliases.lookup(name): return item
  return None

reader = csv.reader(open("data/c/lei/2019-12-05_ra-list-v1.5.csv", "r"))
reader.__next__()

for row in reader:
  slots = [
    ("registration_authority_code", row[0]),
    ("country_name", row[1]),
    ("country_code", row[2]),
    ("country", resolve_name(row[1]))
  ]

  if row[3] != "":
    slots.append(("jurisdiction_name", row[3]))
    jurisdiction = resolve_name(row[3])
    if jurisdiction != None:
      slots.append(("jurisdiction", jurisdiction))

  if row[4] != "":
    slots.append(("register_name", row[4]))
    register = resolve_name(row[4])
    if register != None:
      slots.append(("register", register))

  if row[5] != "":
    slots.append(("native_register_name", row[5]))

  if row[6] != "":
    slots.append(("owner_name", row[6]))
    owner = resolve_name(row[6])
    if owner != None:
      slots.append(("owner", owner))

  if row[7] != "":
    slots.append(("native_owner_name", row[7]))

  if row[8] != "":
    slots.append(("url", row[8]))

  f = kb.frame(slots)

  print(f.data(pretty=True, utf8=True))


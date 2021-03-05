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

"""Convert ISO 20275 ELF legal entity forms to SLING."""

import csv
import sling

# ISO 20275:2017 - Financial services -- Entity legal forms (ELF)
#
# Download:
#  https://www.gleif.org/en/about-lei/code-lists/iso-20275-entity-legal-forms-code-list
#
# Fields:
#  0: ELF Code
#  1: Country of formation
#  2: Country Code (ISO 3166-1)
#  3: Jurisdiction of formation
#  4: Country sub-division code (ISO 3166-2)
#  5: Entity Legal Form name Local name
#  6: Language
#  7: Language Code (ISO 639-1)
#  8: Entity Legal Form name Transliterated name (per ISO 01.140.10)
#  9: Abbreviations Local language
# 10: Abbreviations transliterated
# 11: Date created YYYY-MM-DD (ISO 8601)
# 12: ELF Status ACTV/INAC
# 13: Modification
# 14: Modification date YYYY-MM-DD (ISO 8601)
# 15: Reason

# Load knowledge base.
kb = sling.Store()
kb.load("data/e/kb/kb.sling")

n_id = kb["id"]
n_name = kb["name"]
n_alias = kb["alias"]
n_lang = kb["lang"]
n_applies_to_jurisdiction = kb["P1001"]
n_instance_of = kb["P31"]
n_instance_of = kb["P31"]
n_type_of_business_entity = kb["Q1269299"]
n_country_code = kb["P297"]
n_region_code = kb["P300"]
n_short_name = kb["P1813"]
n_country = kb["P17"]
n_elf = kb["PELF"]

# Build country and region table.
countries = {}
regions = {}
for item in kb:
  code = item[n_country_code]
  if code != None:
    code = kb.resolve(code)
    countries[code] = item
  code = item[n_region_code]
  if code != None:
    code = kb.resolve(code)
    regions[code] = item

# Convert ELF code list to SLING frames.
elffn = "data/c/lei/2020-11-19_elf-code-list-v1.3.csv"
elffile = open(elffn, "r")
elfreader = csv.reader(elffile)
elfreader.__next__()  # skip header
recout = sling.RecordWriter("data/e/lei/elf.rec");
for row in elfreader:
  elf_code = row[0]
  if elf_code == "8888" or elf_code == "9999": continue
  print(elf_code)
  elf_id = "PELF/" + elf_code
  country = countries[row[2]]
  region = regions[row[4]] if len(row[4]) > 0 else None
  name = row[5]
  lang = kb["/lang/" + row[7]]

  abbrlist = row[9]
  abbrlist = abbrlist.replace("&amp;", "&")
  abbrlist = abbrlist.replace(";", ",")
  abbrlist = abbrlist.replace(" and ", ",")
  abbrlist = abbrlist.replace(" or ", ",")
  abbrevs = set()
  for abbrev in abbrlist.split(","):
    abbrev = abbrev.strip()
    if len(abbrev) > 0 and len(abbrev) < 15: abbrevs.add(abbrev)

  jurisdiction = region if region != None else country

  slots = [
    (n_id, elf_id),
    (n_name, kb.qstr(name, lang)),
    (n_instance_of, n_type_of_business_entity),
    (n_applies_to_jurisdiction, jurisdiction),
    (n_country, country),
    (n_elf, elf_code),
  ]
  for a in abbrevs: slots.append((n_short_name, a))

  f = kb.frame(slots)
  recout.write(elf_id, f.data(binary=True))

elffile.close()
recout.close()


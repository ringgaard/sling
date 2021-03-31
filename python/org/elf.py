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

# Wikidata mapping.
wikidata_map = {
  "1MWR": "Q23000060",
  "2EEG": "Q77872838",
  "2HBR": "Q460178",
  "2JZ4": "Q18631232",
  "33MN": "Q64686674",
  "3C7U": "Q7616418",
  "3G3D": "Q96782360",
  "3L58": "Q6428421",
  "4TYO": "Q13641190",
  "50TD": "Q4571771",
  "54M6": "Q17375963",
  "5GGB": "Q49168310",
  "5KU5": "Q43749575",
  "5RDO": "Q56489561",
  "5VCF": "Q18195767",
  "5ZTZ": "Q99691501",
  "6CQN": "Q3742494",
  "6FAI": "Q16917889",
  "6QQB": "Q422062",
  "7QQ0": "Q1480166",
  "81KA": "Q15856579",
  "82QO": "Q87713520",
  "8S9H": "Q99691502",
  "8Z6G": "Q1780029",
  "9DI1": "Q18646132",
  "9HLU": "Q15646299",
  "9LJA": "Q54943230",
  "9T5S": "Q18631228",
  "AMFI": "Q87713484",
  "AXSB": "Q1518609",
  "AZFE": "Q5349747",
  "B5PM": "Q1436139",
  "BEWI": "Q11513034",
  "BKUX": "Q97063639",
  "BL4B": "Q12041908",
  "CATU": "Q61747587",
  "CD28": "Q43751707",
  "CIO8": "Q11863217",
  "D18J": "Q96109123",
  "D1VK": "Q56447357",
  "D2I2": "Q2375914",
  "DKUW": "Q333920",
  "DVXS": "Q10379632",
  "ECAK": "Q6566978",
  "EO9F": "Q97063639",
  "EQOV": "Q875765",
  "ES2D": "Q104438803",
  "F72Z": "Q3591583",
  "FFQL": "Q22927616",
  "FJ0E": "Q64699475",
  "FSBD": "Q95678620",
  "G2I3": "Q61740253",
  "GULL": "Q12334948",
  "GYY6": "Q99528763",
  "H0PO": "Q6832945",
  "H6WW": "Q7257282",
  "H781": "Q15177651",
  "H8VP": "Q15649047",
  "IAP3": "Q976195",
  "IQ9O": "Q12055643",
  "IQGE": "Q14942889",
  "J8PB": "Q56440793",
  "JC0Y": "Q16917171",
  "JCAD": "Q56487597",
  "K5P8": "Q59554118",
  "K65D": "Q98897593",
  "K6VE": "Q30056758",
  "K7XQ": "Q3591586",
  "KBKD": "Q21763580",
  "KHI5": "Q100152139",
  "KMPN": "Q51704864",
  "LJL0": "Q12042877",
  "M9IQ": "Q6428384",
  "MVII": "Q693737",
  "NIJH": "Q19654120",
  "NOI8": "Q159321",
  "NUL8": "Q17050380",
  "O7XB": "Q2624661",
  "OLJ1": "Q87715170",
  "OVKW": "Q56447581",
  "OWUN": "Q423785",
  "P418": "Q3742388",
  "P9F2": "Q56868012",
  "PZ6Y": "Q12301349",
  "Q25I": "Q56467825",
  "QIEL": "Q38911",
  "QJ0F": "Q56457912",
  "QS6A": "Q20057863",
  "QSI2": "Q53828709",
  "QUX1": "Q16584993",
  "QVPB": "Q18195767",
  "QZ3L": "Q9299236",
  "QZIS": "Q18214700",
  "R1JO": "Q423790",
  "R71C": "Q17103304",
  "R85P": "Q422053",
  "SQKS": "Q15734684",
  "T0YJ": "Q19823288",
  "T417": "Q1480166",
  "TNBA": "Q56517350",
  "UFDA": "Q12041652",
  "US8E": "Q5349747",
  "UV02": "Q87713500",
  "UXEW": "Q7383772",
  "UZY3": "Q87715536",
  "V03J": "Q6054513",
  "V44D": "Q19605764",
  "V9QP": "Q98889979",
  "VD7Z": "Q98887929",
  "VIE3": "Q10861788",
  "VJBO": "Q85740306",
  "VSZS": "Q15646299",
  "XH8C": "Q50922133",
  "XHN1": "Q16901839",
  "YJ4C": "Q105816093",
  "XJHM": "Q422007",
  "XLWA": "Q67207116",
  "XTIQ": "Q88537331",
  "YI42": "Q15042660",
  "YK5G": "Q104054205",
  "ZECH": "Q87715173",
  "ZRPO": "Q2624520",
  "ZUHK": "Q28124941",
}

wikidata_submap = {
  "1HXP": "Q1588658",
  "2XIK": "Q695318",
  "5RCH": "Q134161",
  "6CHY": "Q654502",
  "7WRN": "Q728646",
  "8VDW": "Q955214",
  "95G8": "Q154954",
  "9RVC": "Q4539",
  "ANDT": "Q134161",
  "B6ES": "Q5225895",
  "BUMI": "Q279014",
  "BYQJ": "Q10426040",
  "DTAX": "Q955214",
  "FUKI": "Q2912172",
  "I7AS": "Q4539",
  "LUMA": "Q134161",
  "MNQ7": "Q18624259",
  "OJ9I": "Q422007",
  "OSBR": "Q134161",
  "SGST": "Q279014",
  "TMU1": "Q3618366",
  "TPNT": "Q279014",
  "V06W": "Q279014",
  "VYAX": "Q5225895",
  "W2NK": "Q4201895",
  "WCEP": "Q279014",
  "X0SD": "Q166280",
  "YVPW": "Q134161",
  "ZQO8": "Q279014",
}


# Trim string.
def trim(s):
  s = s.strip()
  if len(s) > 120: print("long name:", s)
  return s

# Load knowledge base.
kb = sling.Store()
kb.load("data/e/kb/kb.sling")

n_id = kb["id"]
n_is = kb["is"]
n_name = kb["name"]
n_alias = kb["alias"]
n_lang = kb["lang"]
n_applies_to_jurisdiction = kb["P1001"]
n_instance_of = kb["P31"]
n_subclass_of = kb["P279"]
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
elfs = {}
for row in elfreader:
  elf_code = row[0]
  if elf_code == "8888" or elf_code == "9999": continue
  print(elf_code)
  elf_id = "PELF/" + elf_code
  country = countries[row[2]]
  region = regions[row[4]] if len(row[4]) > 0 else None
  name = trim(row[5])
  lang = kb["/lang/" + row[7]]

  mapping = wikidata_map.get(elf_code)
  submapping = wikidata_submap.get(elf_code)

  abbrlist = row[9]
  abbrlist = abbrlist.replace("&amp;", "&")
  abbrlist = abbrlist.replace(";", ",")
  abbrlist = abbrlist.replace(" and ", ",")
  abbrlist = abbrlist.replace(" or ", ",")
  abbrevs = set()
  for abbrev in abbrlist.split(","):
    abbrev = trim(abbrev)
    if len(abbrev) > 0 and len(abbrev) < 15: abbrevs.add(abbrev)

  jurisdiction = region if region != None else country

  existing = elfs.get(elf_id)
  if existing is None:
    slots = [
      (n_id, elf_id),
      (n_name, kb.qstr(name, lang)),
      (n_instance_of, n_type_of_business_entity),
      (n_applies_to_jurisdiction, jurisdiction),
      (n_country, country),
      (n_elf, elf_code),
    ]
    if mapping: slots.append((n_is, kb[mapping]))
    if submapping: slots.append((n_subclass_of, kb[submapping]))
    for a in abbrevs: slots.append((n_short_name, a))
    elfs[elf_id] = kb.frame(slots)
  else:
    slots = [
      (n_name, kb.qstr(name, lang)),
    ]
    for a in abbrevs: slots.append((n_short_name, a))
    elfs[elf_id].extend(slots)

elffile.close()

# Write output.
recout = sling.RecordWriter("data/e/org/elf.rec");
for elf_id, elf in elfs.items():
  recout.write(elf_id, elf.data(binary=True))
recout.close()


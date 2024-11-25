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

"""Convert 'Statstidende dødsbo proklama' to SLING topics."""

import json
import sling
import sling.flags as flags
import sling.util

flags.define("--db",
             help="database with Statstidende proclamations",
             default=None,
             metavar="DB")

flags.define("--output",
             help="output file",
             default=None,
             metavar="FILE")

flags.parse()

store = sling.Store()
n_id = store["id"]
n_is = store["is"]
n_name = store["name"]
n_description = store["description"]
n_birthname = store["P1477"]
n_type = store["P31"]
n_human = store["Q5"]
n_gender = store["P21"]
n_male = store["Q6581097"]
n_female = store["Q6581072"]
n_residence = store["P551"]
n_address = store["P6375"]
n_postnr = store["P281"]
n_dob = store["P569"]
n_dod = store["P570"]
n_stti = store["PSTTI"]
n_country = store["P27"]
n_denmark = store["Q756617"]
n_spouse = store["P26"]
n_ownerof = store["P1830"]
n_cpr = store["PCPR"]
n_described_by_source = store["P1343"]
n_statstidende = store["Q7604468"]

def get_group(msg, name):
  groups = msg.get("fieldGroups")
  if groups is None: groups = msg.get("fieldgroups")
  for group in groups:
    if group["name"] == name: return group

def get_field(fields, name):
  for field in fields:
    if field.get("name") == name:
      value = field["value"]
      if type(value) is str: value = value.strip()
      return value

def remove_trailing_zeros(s):
  while True:
    pos = s.find(" 0")
    if pos == -1: return s
    s = s[:pos + 1] + s[pos + 2:]

# Read Danish postal codes.
n_copenhagen = store["Q1748"]
n_frederiksberg = store["Q30096"]

fin = open("data/org/dkpostnr.txt")
postalcodes = {}
for postnr in range(1000, 1800): postalcodes[postnr] = n_copenhagen
for postnr in range(1800, 2000): postalcodes[postnr] = n_frederiksberg

for line in fin:
  line = line.strip()
  if len(line) == 0: continue;
  fields = line.split(",")
  postnr = fields[0]
  qid = fields[2]
  postalcodes[int(postnr)] = store[qid]
fin.close()

addresses = {}
cprs = set()

db = sling.Database(flags.arg.db)
recout = None
if flags.arg.output:
  recout = sling.RecordWriter(flags.arg.output)

def build_person(fields, builder):
  cpr = get_field(fields, "CPR-nr.")
  doed = get_field(fields, "Dødsdato")
  navn = get_field(fields, "Navn")
  ungnavn = get_field(fields, "Efternavn ved fødsel")
  vej = get_field(fields, "Vej")
  if vej is None: vej = get_field(fields, "Vejnavn")
  if vej is None: vej = ""
  etage = get_field(fields, "Etage")
  husnr = get_field(fields, "Husnr.")
  side = get_field(fields, "Side/dørnr")
  postnr = get_field(fields, "Postnr")
  by = get_field(fields, "By")
  cprs.add(cpr)

  # Construct name, postnr, and address for legacy records.
  legacy = False
  if navn is None:
    field_list = []
    for f in fields:
      if f.get("type") == 1: field_list.append(f["value"])
    if len(field_list) == 0: return False
    navn = field_list[0]
    vej = ", ".join(field_list[1:])
    postnr = field_list[-1].split(" ")[0]
    legacy = True

  # Trim name.
  paren = navn.find("(")
  if paren != -1:
    print("trim", cpr, navn)
    navn = navn[:paren - 1].strip()

  # Residence.
  address = remove_trailing_zeros(vej)
  if husnr: address += " " + husnr
  if etage: address += " " + etage
  if side: address += " " + side
  if postnr and not legacy: address += ", " + postnr
  if by: address += " " + by
  address = address.strip()
  if len(address) == 0: address = None

  # Check for existing address.
  if address and postnr:
    akey = cpr + str(postnr) + address[0:4]
    if akey in addresses:
      if address != addresses[akey]:
        #print("replace address %s: '%s' -> '%s'" %
        #      (akey, address, addresses[akey]))
        address = addresses[akey]
    else:
      addresses[akey] = address

  # Birth name.
  birthname = None
  if ungnavn and not navn.endswith(ungnavn):
    pos = navn.rfind(" ")
    if pos != -1:
      birthname = navn[:pos + 1] + ungnavn

  # Birth date, see https://www.cpr.dk/media/12066/personnummeret-i-cpr.pdf
  series = int(cpr[6])
  if series == 0 or series == 1 or series == 2 or series == 3:
    born = "19" + cpr[4:6]
  elif series == 4 or series == 9:
    if cpr[4:6] < "37":
      born = "20" + cpr[4:6]
    else:
      born = "19" + cpr[4:6]
  elif series == 5 or series == 6 or series == 7 or series == 8:
    if cpr[4:6] < "58":
      born = "20" + cpr[4:6]
    else:
      born = "18" + cpr[4:6]

  died = None
  if doed:
    if "-" in doed:
      died = doed[0:4]
    else:
      died = doed[6:10]

  dob = int(born + cpr[2:4] + cpr[0:2])
  dod = None
  if doed:
    if "-" in doed:
      dod = int(doed.replace("-", ""))
    else:
      dod = int(doed[6:10] + doed[3:5] + doed[0:2])

  builder.add(n_id, "PCPR/" + cpr)
  builder.add(n_name, navn)
  builder.add(n_birthname, birthname)
  if dod: builder.add(n_description, born + "-" + died)
  builder.add(n_type, n_human)
  if int(cpr[9]) % 2 == 0:
    builder.add(n_gender, n_female)
  else:
    builder.add(n_gender, n_male)
  builder.add(n_dob, dob)
  builder.add(n_dod, dod)

  residence = by
  if postnr is not None and postnr.isdigit():
    location = postalcodes.get(int(postnr))
    if location is not None: residence = location

  builder.add(n_residence, store.frame([
    (n_is, residence),
    (n_address, address)
  ]))
  builder.add(n_country, n_denmark)
  builder.add(n_described_by_source, n_statstidende)
  return True

num_messages = 0
num_persons = 0

for key, _, rec in db:
  proklama = json.loads(rec.decode())
  msgid = proklama["messageNumber"]
  num_messages += 1

  deceased = get_group(proklama, "Afdøde")
  if deceased is None:
    print("missing deceased:", msgid)
    continue

  # Deceased person.
  fields = deceased.get("fields")

  cpr = get_field(fields, "CPR-nr.")
  builder = sling.util.FrameBuilder(store)
  build_person(fields, builder)

  # Deceased's spouse.
  spouse = get_group(proklama,
                     "Tidligere afdød ægtefælle (kun ved uskiftet bo)")
  if spouse:
    sfields = spouse["fields"]
    scpr = get_field(sfields, "CPR-nr.")
    if scpr:
      sbuilder = sling.util.FrameBuilder(store)
      if build_person(sfields, sbuilder):
        sbuilder.add(n_cpr, scpr)
        sbuilder.add(n_spouse, store.frame("PCPR/" + cpr))
        builder.add(n_spouse, store.frame("PCPR/" + scpr))
        sf = sbuilder.write(recout)
        if recout is None: print(sf)
        num_persons += 1

  # Deceased's business.
  business = get_group(proklama, "Afdødes forretning")
  if business:
    bfields = business["fields"]
    cvr = get_field(bfields, "CVR-nr.")
    if cvr and cvr != "Intet CVR-nr":
      builder.add(n_ownerof, store.frame("P1059/" + cvr))

  builder.add(n_cpr, cpr)
  builder.add(n_stti, msgid)

  f = builder.write(recout)
  if recout is None: print(f)
  num_persons += 1


if recout: recout.close()
db.close()
print(num_messages, "messages", num_persons, "persons", len(cprs), "CPRs")

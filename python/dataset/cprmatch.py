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

""" Match persons in Statstidende to SLING topics."""

import json
import sling
import sling.flags as flags
import sling.util

flags.define("--db",
             help="database with Statstidende proclamations",
             default=None,
             metavar="DB")

flags.define("--checkpoint",
             help="file with latest checkpoint for scanning statstidende",
             default=None,
             metavar="FILE")

flags.parse()

def get_group(msg, name):
  groups = msg.get("fieldGroups")
  if groups is None: groups = msg.get("fieldgroups")
  for group in groups:
    if group["name"] == name: return group

def get_field(fields, name):
  for field in fields:
    if field.get("name") == name: return field.get("value")

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
n_ownerof = kb["P1830"]
n_ownedby = kb["P127"]

aliases = sling.PhraseTable(kb, "data/e/kb/da/phrase-table.repo")

db = sling.Database(flags.arg.db)
chkpt = sling.util.Checkpoint(flags.arg.checkpoint)

# Collect CPR numbers from proclamations.
cprnumbers = []
for key, _, rec in db(begin=chkpt.checkpoint):
  proklama = json.loads(rec.decode())
  msgid = proklama["messageNumber"]
  deceased = get_group(proklama, "Afdøde")
  if deceased is None: continue
  fields = deceased["fields"]
  cpr = get_field(fields, "CPR-nr.")
  if cpr is None: cpr = get_field(fields, "CPR.nr.")
  cprnumbers.append(cpr)
  spouse = get_group(proklama,
                     "Tidligere afdød ægtefælle (kun ved uskiftet bo)")
  if spouse:
    sfields = spouse["fields"]
    scpr = get_field(sfields, "CPR-nr.")
    if scpr is None: scpr = get_field(sfields, "CPR.nr.")
    if scpr: cprnumbers.append(scpr)

# Match persons.
for cprnr in cprnumbers:
  itemid = "PCPR/" + cprnr
  if itemid not in kb:
    print(";", cprnr, "not found")
    continue

  item = kb[itemid]
  name = item[n_name]
  born = item[n_birthdate]
  if name is None:
    print("; No name:", item)
    continue

  for m in aliases.lookup(name):
    if m is None: continue
    if m == item: continue
    if m[n_instanceof] != n_human: continue
    dob = m[n_birthdate]
    if dob is None: continue
    if dob == born:
      print('{name:"%s" +"%s" +"%s"}' % (name, itemid, m.id))

  business = item[n_ownerof]
  if business:
    owners = list(business(n_ownedby))
    if len(owners) == 1 and kb.resolve(owners[0])[n_name] == name:
      print('{name:"%s" +"%s" +"%s"}' % (name, itemid, kb.resolve(owners[0]).id))
    else:
      print('{name:"%s" +"%s" P1830: %s}' % (name, itemid, business.id))

chkpt.commit(db.epoch())
db.close()

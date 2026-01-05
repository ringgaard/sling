# Copyright 2022 Ringgaard Research ApS
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

"""Make list of persons with birthdays."""

import json
import sling
import sling.flags as flags
import sling.media.photo as photolib

flags.define("--date",
             help="date for birthdays",
             default=None,
             metavar="YYYY-MM-DD")

flags.define("--output",
             help="output file for birthday list",
             default="/var/data/corpora/media/birthdays.json",
             metavar="FILE")

flags.parse()
date = sling.Date(flags.arg.date)

kb = sling.Store()
kb.load("data/e/kb/kb.sling")
cal = sling.Calendar(kb)

n_instance_of = kb["P31"]
n_name = kb["name"]
n_description = kb["description"]
n_human = kb["Q5"]
n_dob = kb["P569"]
n_dod = kb["P570"]
n_popularity = kb["/w/item/popularity"]
n_image = kb["P18"]
n_media = kb["media"]
n_lex = kb["lex"]

def is_human(item):
  for t in item(n_instance_of):
    if t == n_human: return True
  return False

def popularity(item):
  return item[n_popularity] if n_popularity in item else 0

num_dob = 0
num_bday = 0
persons = []

for item in kb:
  if n_dob not in item: continue
  if n_dod in item: continue
  if not is_human(item): continue
  num_dob += 1

  dob = sling.Date(item[n_dob])
  if dob.day != date.day or dob.month != date.month: continue
  age = date.year - dob.year
  if age < 10 or age > 100: continue
  num_bday += 1

  if age % 10 == 0 or (popularity(item) > 1000 and age % 5 == 0):
    persons.append(item)

persons.sort(key=lambda item: popularity(item), reverse=True)
birthdays = []
for item in persons[:30]:
  dob = sling.Date(item[n_dob])
  age = date.year - dob.year

  picture = None
  if n_image in item:
    fn = kb.resolve(item[n_image])
    if type(fn) is str: picture = photolib.commons_media(fn)
  if picture is None:
    for m in item(n_media):
      url = kb.resolve(m)
      if not url.startswith("!"):
        picture = url
        break

  name = item[n_name]
  description = item[n_description]
  print(item.id, age, dob, name, description)

  person = {
    "id": item.id,
    "birthdate": cal.str(dob),
    "age": age,
  }
  if name is not None: person["name"] = str(name)
  if description is not None: person["description"] = str(description)
  if picture is not None: person["picture"] = picture

  birthdays.append(person)

report = {
  "date": cal.str(date),
  "birthdays": birthdays,
}

with open(flags.arg.output, "w") as f:
  f.write(json.dumps(report, indent=2))

print(num_dob, "with dob", num_bday, "birthdays")

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

"""Convert Danish NACE codes to SLING."""

import sys
import sling

store = sling.Store()
n_id = store["id"]
n_name = store["name"]
n_description = store["description"]
n_instance_of = store["P31"]
n_subclass_of = store["P279"]
n_economic_activity = store["Q8187769"]
n_nace = store["Q732298"]
n_nace_code = store["P4496"]
n_isic_code = store["P1796"]
n_danish = store["/lang/da"]

s_industry_code = store.qstr("branchekode", n_danish)

output = sling.RecordWriter("data/e/org/dknace.rec")
fin = open("data/c/sic/dknace.txt")
for line in fin:
  line = line.strip()
  if len(line) == 0: continue
  nace = line[0:6]
  label = line[7:]

  code1 = int(nace[0:2])
  code2 = int(nace[2:4])
  code3 = int(nace[4:6])

  code = str(code1) + "." + str(code2)
  parent = ""
  if code3 != 0:
    parent = code
    code = code + "." + str(code3) + "DK"
  elif code2 != 0:
    parent = str(code1)

  slots = []
  slots.append((n_id, "P4496/" + code))
  slots.append((n_name, store.qstr(label.lower(), n_danish)))
  slots.append((n_description, s_industry_code))
  slots.append((n_instance_of, n_economic_activity))
  if parent != "": slots.append((n_subclass_of, store["P4496/" + parent]))
  slots.append((n_nace_code, code))

  frame = store.frame(slots)
  output.write(frame.id, frame.data(binary=True))

  print(code, parent, label)

fin.close()
output.close()


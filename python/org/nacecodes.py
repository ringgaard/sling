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

"""Convert NACE codes to SLING."""

import xml.etree.ElementTree as etree
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

output = sling.RecordWriter("data/e/org/nace.rec")

tree = etree.parse("data/c/sic/NACE_REV2_20190315_170746.xml")
root = tree.getroot()
classification = tree.find("Classification")

levels = [None] * 5
levels[0] = ""

for item in classification:
  if item.tag != "Item": continue
  code = item.attrib["id"]
  level = int(item.attrib["idLevel"])
  parent = levels[level - 1]

  label = None
  for l in item.iter("LabelText"):
    label = l.text

  isic = None
  for p in item.iter("Property"):
    if p.get("genericName") == "ISIC4_REF":
      isic = p.find("PropertyQualifier").find("PropertyText").text

  slots = []
  slots.append((n_id, "P4496/" + code))
  slots.append((n_name, label.lower()))
  slots.append((n_description, "economic activity"))
  slots.append((n_instance_of, n_economic_activity))
  if parent != "": slots.append((n_subclass_of, store["P4496/" + parent]))
  slots.append((n_nace_code, code))
  if isic != None: slots.append((n_isic_code, isic))

  frame = store.frame(slots)
  output.write(frame.id, frame.data(binary=True))

  #print(code, parent, isic, label)
  levels[level] = code

output.close()


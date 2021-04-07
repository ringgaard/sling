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

"""Import FactGrid and convert to SLING format."""

import sling
import gzip

# Map ids into FactGrid namespace.
def convert_id(idstr):
  if idstr[0] == "P" or idstr[0] == "Q": return "P8168/" + idstr;
  return idstr

# Initialize commons store.
commons = sling.Store()
n_id = commons["id"]
n_is = commons["is"]
n_isa = commons["isa"]
n_qid = commons["/w/qid"]
n_pid = commons["P343"]
n_property = commons["/w/property"]
n_fg_item_id = commons["P8168"]

wikiconv = sling.WikiConverter(commons, "en")

commons.freeze()

# Read all items from FactGrid dump.
fin = gzip.open("data/c/wikidata/factgrid.json.gz")
fitem = sling.RecordWriter("data/e/factgrid/factgrid-items.rec")
fprop = sling.RecordWriter("data/e/factgrid/factgrid-properties.rec")
num_items = 0
num_properties = 0
for line in fin:
  # Trim line.
  if len(line) < 3: continue
  line = line[:line.rfind(b'}') + 1]

  # Convert from JSON to SLING.
  store = sling.Store(commons)
  item, revision = wikiconv.convert_wikidata(store, line)

  # Map ids from FactGrid to Wikidata ids.
  slots = []
  fgid = None
  for name, value in item:
    if name == n_id:
      # Convert ids to FG ids.
      fgid = value
      value = convert_id(value)
    elif name == n_qid or name == n_pid:
      # Add Wikidata redirects for mapped items and properties.
      name = n_is
      value = store[value]
    else:
      # Map statements.
      name = store[convert_id(name.id)]
      if type(value) is sling.Frame:
        if value.ispublic():
          value = store[convert_id(value.id)]
        else:
          # Map qualifiers.
          qslots = []
          for qname, qvalue in value:
            qname = store[convert_id(qname.id)]
            if type(qvalue) is sling.Frame:
              if qvalue.ispublic():
                qvalue = store[convert_id(qvalue.id)]
            qslots.append((qname, qvalue))
          value = store.frame(qslots)

    slots.append((name, value))

  # Add link to FactGrid.
  if fgid: slots.append((n_fg_item_id, fgid))

  # Write mapped item/property to output.
  mapped_item = store.frame(slots)
  if mapped_item[n_isa] == n_property:
    fprop.write("P8168/%s" % fgid, mapped_item.data(binary=True))
    num_properties += 1
  else:
    fitem.write("P8168/%s" % fgid, mapped_item.data(binary=True))
    num_items += 1

# Add FactGrid xref property.
store = sling.Store(commons)
fgprop = store.parse("""
{
  =P8168
  :/w/property
  name: "FactGrid item ID"@/lang/en
  description: "identifier for an item in FactGrid"@/lang/en
  source: /w/entity
  target: /w/xref
  P31: P8168/Q21878
  P1630: "https://database.factgrid.de/wiki/Item:$1"
}""")
fprop.write("P8168", fgprop.data(binary=True))

fin.close()
fitem.close()
fprop.close()
print(num_items, "items", num_properties, "properties")


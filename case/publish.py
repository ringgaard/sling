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

"""SLING case publisher"""

import datetime

import sling
import sling.util
import sling.flags as flags
import sling.log as log

flags.define("--casedb",
             help="database for shared cases",
             default="case",
             metavar="DB")

flags.define("--output",
             help="Output file with published items",
             default=None,
             metavar="FILE")

flags.parse()

# Connect to case database.
casedb = sling.Database(flags.arg.casedb, "publish.py")

# Output file.
recout = sling.RecordWriter(flags.arg.output)

# Commons store.
commons = sling.Store()
n_id = commons["id"]
n_name = commons["name"]
n_caseno = commons["caseno"]
n_main = commons["main"]
n_publish = commons["publish"]
n_modified = commons["modified"]
n_topics = commons["topics"]
n_has_part = commons["P527"]
n_publication_date = commons["P577"]
n_described_by_source = commons["P1343"]
commons.freeze()

# Output all published cases.
num_cases = 0
num_published = 0
num_topics = 0
for rec in casedb.values():
  # Parse case file.
  num_cases += 1
  store = sling.Store(commons)
  casefile = store.parse(rec)

  # Only output published cases.
  if not casefile[n_publish]: continue
  num_published += 1

  # Get case metadata.
  caseno = casefile[n_caseno]
  modified = casefile[n_modified]
  publication_date = sling.Date(modified)
  main = casefile[n_main]
  topics = casefile[n_topics]
  print(f"Publish case #{caseno}: {main[n_name]}")

  # Build case item.
  slots = []
  slots.append((n_id, "c/" + str(caseno)))

  # Case title.
  name = store.resolve(main[n_name])
  if name:
    slots.append((n_name, f"Case #{caseno}: {name}"))

  # Case publication date.
  slots.append((n_publication_date, publication_date.value()))

  # Additional case properties.
  for name, value in main:
    if name != n_id and name != n_name:
      slots.append((name, value))

  # Case topis.
  for topic in topics:
    if topic == main: continue
    slots.append((n_has_part, topic))

  # Write case item to output.
  caseitem = store.frame(slots)
  recout.write(caseitem.id, caseitem.data(binary=True))

  # Build topic items.
  for topic in topics:
    if topic == main: continue
    topic.append(n_described_by_source, caseitem)
    recout.write(topic.id, topic.data(binary=True))
    num_topics += 1

recout.close()

print(f"{num_published}/{num_cases} cases published with {num_topics} topics")


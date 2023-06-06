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
n_is = commons["is"]
n_name = commons["name"]
n_caseid = commons["caseid"]
n_main = commons["main"]
n_publish = commons["publish"]
n_modified = commons["modified"]
n_topics = commons["topics"]
n_internal = commons["internal"]
n_media = commons["media"]
n_lex = commons["lex"]
n_has_part = commons["P527"]
n_publication_date = commons["P577"]
n_described_by_source = commons["P1343"]
n_topic_id = commons["PTOPIC"]
commons.freeze()

# Output all published cases.
num_cases = 0
num_published = 0
num_topics = 0
num_new_topics = 0
for rec in casedb.values():
  # Parse case file.
  num_cases += 1
  store = sling.Store(commons)
  casefile = store.parse(rec)

  # Only output published cases.
  if not casefile[n_publish]: continue
  num_published += 1

  # Get case metadata.
  caseid = casefile[n_caseid]
  modified = casefile[n_modified]
  publication_date = sling.Date(modified)
  main = casefile[n_main]
  topics = casefile[n_topics]

  # Update case title.
  name = main[n_name]
  if name:
    main[n_name] = f"Case #{caseid}: {name}"
  else:
    main[n_name] = f"Case #{caseid}"

  # Add case publication date.
  main[n_publication_date] = publication_date.value()

  # Add case topics.
  for topic in topics:
    if topic == main: continue
    main.append(n_has_part, topic)

  # Write case item to output.
  recout.write(main.id, main.data(binary=True))

  # Build topic items.
  num_new = 0
  num_photos = 0
  for topic in topics:
    # Do not publish main topic.
    if topic == main: continue

    # Remove notes, documents, and internals.
    del topic[None]
    del topic[n_internal]
    del topic[n_lex]

    # Add case source to topic.
    topic.append(n_described_by_source, main)

    # Add topic reference.
    topic.append(n_topic_id, topic[n_id][2:])

    num_topics += 1
    num_photos += topic.count(n_media)
    if topic.get(n_is) is None:
      num_new += 1
      num_new_topics += 1
    else:
      # Update string-based references.
      redirs = []
      update = False
      for redir in topic(n_is):
        if type(redir) is str:
          update = True
          redirs.append((n_is, store[redir]))
        else:
          redirs.append((n_is, redir))
      if update:
        del topic[n_is]
        topic.extend(redirs)

    recout.write(topic.id, topic.data(binary=True))

  print(f"Publish case #{caseid}: {main[n_name]}",
        f"({num_new}/{len(topics)} new topics, {num_photos} photos)")

recout.close()

print(f"{num_published}/{num_cases} cases published",
      f"with {num_new_topics}/{num_topics} new topics")


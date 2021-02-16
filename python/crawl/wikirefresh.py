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

"""Refresh qids from Wikidata."""

import json
import requests
import sling
import sling.flags as flags

flags.define("--qids",
             help="file with list of QIDs",
             metavar="FILE")

flags.define("--dburl",
             help="wiki database url",
             default="http://localhost:7070/wikidata",
             metavar="URL")

flags.define("--wiki_fetch_url",
             help="url for fetching items from wikidata",
             default="https://www.wikidata.org/wiki/Special:EntityData",
             metavar="URL")

flags.define("--string_buckets",
             help="number of buckets for for string coalescing",
             default=4096,
             type=int,
             metavar="NUM")

flags.parse()

# Commons store for Wikidata converter.
commons = sling.Store()
wikiconv = sling.WikiConverter(commons)
commons.freeze()

# Global variables.
dbsession = requests.Session()
wdsession = requests.Session()

# Fetch item and update database.
def update_item(qid):
  # Fetch item revision from Wikidata.
  url = "%s?id=%s&format=json" % (flags.arg.wiki_fetch_url, qid)
  reply = wdsession.get(url)

  # Convert item to SLING format.
  store = sling.Store(commons)
  item, revision = wikiconv.convert_wikidata(store, reply.text)

  # Coalese strings.
  store.coalesce(flags.arg.string_buckets)

  # Save item in database.
  print(qid, revision)
  reply = dbsession.put(
    flags.arg.dburl + "/" + qid,
    data=item.data(binary=True),
    headers={
      "Version": str(revision),
      "Mode": "ordered",
    }
  )
  reply.raise_for_status()

with open(flags.arg.qids) as f: qids = f.read().split("\n")
for qid in qids:
  if len(qid) == 0: continue
  update_item(qid)


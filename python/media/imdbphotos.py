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

"""Fetch photos from IMDB."""

import collections
import requests

import sling
import sling.flags as flags

flags.define("--kb",
             default="data/e/kb/kb.sling",
             help="Knowledge base")

flags.parse()

#user_agent = "SLING/1.0 bot (https://github.com/ringgaard/sling)"
#session = requests.Session()

# Load knowledge base.
kb = sling.Store()
kb.load(flags.arg.kb)

n_image = kb["P18"]
n_media = kb["media"]
n_imdb = kb["P345"]


# Find items with IMDB id but no photos.
types = collections.defaultdict(int)
num_noimage = 0
for item in kb:
  imdb = item[n_imdb]
  if imdb is None: continue
  imdb = kb.resolve(imdb)
  kind = imdb[0:2]
  types[kind] += 1
  if kind != "nm": continue
  if item[n_image] != None or item[n_media] != None:
    continue
  print(item.id, imdb, item.name)
  num_noimage += 1

print("noimage", num_noimage, "types", types)


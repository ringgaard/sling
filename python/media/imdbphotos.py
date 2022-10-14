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

"""Fetch photo profiles from IMDB."""

import requests
import json

import sling
import sling.flags as flags

flags.define("--kb",
             default="data/e/kb/kb.sling",
             help="Knowledge base")

flags.define("--imdb",
             help="database for storing IMDB profiles",
             default="imdb",
             metavar="DB")

flags.parse()
session = requests.Session()
db = sling.Database(flags.arg.imdb, "imdbphotos")

# Load knowledge base.
kb = sling.Store()
kb.load(flags.arg.kb)
p_id = kb["id"]
p_is = kb["is"]
p_media = kb["media"]
p_imdb = kb["P345"]
p_stated_in = kb["P248"]
p_reason_for_deprecation = kb["P2241"]
n_imdb = kb["Q37312"]
empty_profile = kb.frame([])
kb.freeze()

# Find items with IMDB id.
num_profiles = 0
num_items = 0
num_photos = 0
for item in kb:
  # Check for IMDB id.
  num_items += 1
  if p_imdb not in item: continue

  # Find last IMDB id that is not deprecated.
  imdbid = None
  for v in item(p_imdb):
    if v is None: continue
    if type(v) is sling.Frame:
      if v[p_reason_for_deprecation] != None: continue
      imdbid = v[p_is]
    else:
      imdbid = v

  if imdbid is None: continue
  if type(imdbid) is not str:
    print("Bad IMDB id for", item.id, imdbid)
    continue

  # Skip unless it is a person.
  kind = imdbid[0:2]
  if kind != "nm": continue

  # Skip if profile already in database.
  key = "P345/" + imdbid
  if key in db: continue

  # Fetch info from IMDB.
  r = session.get("https://sg.media-imdb.com/suggests/n/%s.json" % imdbid)
  if r.status_code == 503:
    print("UNAVAILABLE", item.id, imdbid, item.name)
    continue
  elif r.status_code == 400 or r.status_code == 404:
    print("NOTFOUND", item.id, imdbid, item.name)
    continue
  r.raise_for_status()
  data = r.content[r.content.find(b'(') + 1 : -1]
  info = json.loads(data)

  # Get image url.
  if len(info["d"]) == 0:
    print("EMPTY", item.id, imdbid, item.name)
    continue
  else:
    d = info["d"][0]
    i = d.get("i")

  if i is None or len(i) == 0:
    # Create empty profile if there is no photo.
    profile = empty_profile
    url = "(no photo)"
  else:
    # Create profile for person.
    url = i[0].replace("/ia.media-imdb.com/", "/m.media-amazon.com/")
    store = sling.Store(kb)
    image = store.frame([(p_is, url), (p_stated_in, n_imdb)])
    profile = store.frame([(p_media, image), (p_imdb, imdbid)])
    num_photos += 1

  # Save profile.
  db[key] = profile.data(binary=True)

  num_profiles += 1
  print(num_items, num_profiles, item.id, imdbid, item.name, url)

print(num_items, "items", num_profiles, "imdb profiles", num_photos, "photos")


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

"""Find and remove duplicate photos."""

import hashlib
import requests
import sys
import sling
import sling.flags as flags

flags.define("--id",
             default=None,
             help="Item id photo updates")

flags.define("--photodb",
             help="database for photo profiles",
             default="vault/photo",
             metavar="DB")

flags.define("--mediadb",
             help="database for images",
             default="vault/media",
             metavar="DB")

flags.define("--all",
             help="check all profiles",
             default=False,
             action="store_true")

flags.define("--dedup",
             help="remove duplicates from profile",
             default=False,
             action="store_true")

flags.parse()

bad_photos = set([
  b'\xf1{\x01\x90\x1cu,\x1b\xb0I(\x13\x1d\x16a\xaf', # i.reddit.it
  b'\xd85\x88Cs\xf4\xd6\xc8\xf2GB\xce\xab\xe7IF',    # i.imgur.com/removed.png
])

photodb = sling.Database(flags.arg.photodb, "photodedup.py/photo")
mediadb = sling.Database(flags.arg.mediadb, "photodedup.py/media")

store = sling.Store()
n_media = store["media"]
n_is = store["is"]
n_has_quality = store["P1552"]
n_nsfw = store["Q2716583"]

def dedup(id, profile):
  # Compute image hash for each photo to detect duplicates.
  fingerprints = {}
  duplicates = set()
  missing = set()
  naughty = set()
  num_photos = 0
  num_duplicates = 0
  num_missing = 0
  for media in profile(n_media):
    url = store.resolve(media)
    nsfw = type(media) is sling.Frame and media[n_has_quality] == n_nsfw

    # Get image from database or fetch directly.
    image = mediadb[url]
    if image is None:
      r = requests.get(url)
      if r.status_code != 200:
        print(url, "missing", r.status_code)
        missing.add(url)
        num_missing += 1
        continue
      print(url, "not cached")
      image = r.content

    # Compute hash and check for duplicates.
    fingerprint = hashlib.md5(image).digest()
    dup = fingerprints.get(fingerprint)
    if dup != None:
      duplicates.add(url)
      num_duplicates += 1

      # Check for inconsistent nsfw classification.
      if nsfw:
        if dup not in naughty:
          print(id, url, " nsfw duplicate of", dup)
        else:
          print(id, url, " duplicate of", dup)
      else:
        if dup in naughty:
          print(id, url, " sfw duplicate of", dup)
        else:
          print(id, url, " duplicate of", dup)
    elif fingerprint in bad_photos:
      missing.add(url)
      num_missing += 1
      print(id, url, " bad")
    else:
      fingerprints[fingerprint] = url

    if nsfw: naughty.add(url)
    num_photos += 1

  print(id,
        num_photos, "photos,",
        num_duplicates, "duplicates",
        num_missing, "missing")

  # Remove duplicates.
  if (num_duplicates > 0 or num_missing > 0) and flags.arg.dedup:
    # Find photos to keep.
    keep = []
    for media in profile(n_media):
      url = store.resolve(media)
      if url in duplicates or url in missing:
        print(id, "Remove", url)
      else:
        keep.append((n_media, media))
    del profile[n_media]
    profile.extend(keep)

  return num_duplicates + num_missing

if flags.arg.all:
  # Check all profiles.
  for id, _, data in photodb:
    profile = store.parse(data)
    removed = dedup(id, profile)
    if removed > 0 and flags.arg.dedup:
      # Write updated profile.
      photodb[id] = profile.data(binary=True)
else:
  # Check single profile.
  data = photodb[flags.arg.id]
  if data is None:
    print("no profile found for", id)
  else:
    profile = store.parse(data)
    removed = dedup(flags.arg.id, profile)
    if removed > 0 and flags.arg.dedup:
      # Write updated profile.
      photodb[flags.arg.id] = profile.data(binary=True)


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

flags.define("--dedup",
             help="remove duplicates from profile",
             default=False,
             action="store_true")

flags.parse()

photodb = sling.Database(flags.arg.photodb)
mediadb = sling.Database(flags.arg.mediadb)

store = sling.Store()
n_media = store["media"]
n_is = store["is"]
n_has_quality = store["P1552"]
n_nsfw = store["Q2716583"]

# Read item photo profile from database.
data = photodb[flags.arg.id]
if data is None:
  print("no profile found")
  sys.exit(1)
profile = store.parse(data)

# Compute image hash for each photo to detect duplicates.
fingerprints = {}
duplicates = set()
naughty = set()
num_photos = 0
num_duplicates = 0
for media in profile(n_media):
  url = store.resolve(media)
  nsfw = type(media) is sling.Frame and media[n_has_quality] == n_nsfw

  # Get image from database.
  image = mediadb[url]
  if image is None:
    print(url, "missing in database")
    continue

  # Compute hash and check for duplicates.
  fingerprint = hashlib.md5(image).digest()
  dup = fingerprints.get(fingerprint)
  if dup != None:
    duplicates.add(url)
    num_duplicates += 1

    # Check for inconsistent nsfw classification.
    if nsfw:
      if dup not in naughty:
        print(url, " nsfw duplicate of", dup)
      else:
        print(url, " duplicate of", dup)
    else:
      if dup in naughty:
        print(url, " sfw duplicate of", dup)
      else:
        print(url, " duplicate of", dup)
  else:
    fingerprints[fingerprint] = url

  if nsfw: naughty.add(url)
  num_photos += 1

print(num_photos, "photos,", num_duplicates, "duplicates")

# Remove duplicates
if num_duplicates > 0 and flags.arg.dedup:
  # Find photos to keep.
  keep = []
  for media in profile(n_media):
    url = store.resolve(media)
    if url in duplicates:
      print("Remove", url)
    else:
      keep.append((n_media, media))
  del profile[n_media]
  profile.extend(keep)

  # Write updated profile.
  photodb[flags.arg.id] = profile.data(binary=True)


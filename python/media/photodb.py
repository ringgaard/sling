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

"""Add photos to photo database."""

import json
import requests
import os
import re
import sling
import sling.flags as flags

flags.define("--id",
             default=None,
             help="Item id photo updates")

flags.define("--photodb",
             help="database for photo profiles",
             default="http://vault:7070/photo",
             metavar="DBURL")

flags.define("--imgurkeys",
             default="local/keys/imgur.json",
             help="Imgur API key file")

flags.define("--nsfw",
             help="mark photos as nsfw",
             default=False,
             action="store_true")

flags.define("url",
             nargs="*",
             help="photo URLs",
             metavar="URL")

flags.parse()
session = requests.Session()

store = sling.Store()
n_media = store["media"]
n_is = store["is"]
n_legend = store["P2096"]
n_statement_subject_of = store["P805"]
n_nsfw = store["Q2716583"]

# Read item photo profile from database.
def read_profile(itemid):
  r = session.get(flags.arg.photodb + "/" + itemid)
  if r.status_code == 404: return None
  r.raise_for_status()
  profile = store.parse(r.content)
  return profile

# Write item photo profile to database.
def write_profile(itemid, profile):
  content = profile.data(binary=True)
  r = session.put(flags.arg.photodb + "/" + itemid, data=content)
  r.raise_for_status()

# Get API keys for Imgur.
imgurkeys = None
if os.path.exists(flags.arg.imgurkeys):
  with open(flags.arg.imgurkeys, "r") as f:
    imgurkeys = json.load(f)

# Read photo profile for item.
profile = read_profile(flags.arg.id)
if profile is None: profile = store.frame({})
#print("profile", profile)

# Get existing set of photo urls.
photos = set()
for media in profile(n_media):
  photos.add(store.resolve(media))
if len(photos) > 0: print(len(photos), "exisiting photos")

# Fetch photo urls.
updated = False
num_photos = 0
for url in flags.arg.url:
  #print("URL", url)

  # Check for imgur album.
  m = re.match("https://imgur.com/a/(\w+)", url)
  if m != None:
    albumid = m.group(1)
    print("Imgur album", albumid)
    auth = {
      'Authorization': "Client-ID " + imgurkeys["clientid"]
    }
    r = session.get("https://api.imgur.com/3/album/" + albumid, headers=auth)
    r.raise_for_status()
    reply = r.json()["data"]
    #print(json.dumps(reply, indent=2))

    serial = 1
    total = len(reply["images"])
    title = reply["title"]
    for image in reply["images"]:
      # Photo URL.
      link = image["link"]
      if link in photos:
        print("Skip existing photo", link)
        continue

      # Skip anmated GIFs.
      if (image["animated"]):
        print("Skipping animated image", link);
        continue

      # Image caption.
      caption = image["title"]
      if caption is None and title != None:
        caption = title + " (%d/%d)" % (serial, total)

      # NSFW flag.
      nsfw = flags.arg.nsfw or reply["nsfw"] or image["nsfw"]

      print("Add", link,
            caption if caption != None else "",
            "NSFW" if nsfw else "")

      # Add media frame to profile.
      slots = [(n_is, link)]
      if caption: slots.append((n_legend, caption))
      if nsfw: slots.append((n_statement_subject_of, n_nsfw))
      frame = store.frame(slots)
      profile.append(n_media, frame)
      photos.add(link)

      num_photos += 1
      serial += 1
      updated = True

# Write profile.
if updated:
  print("updating", flags.arg.id, num_photos, "photos added")
  #print("updated", profile.data(pretty=True))
  write_profile(flags.arg.id, profile)


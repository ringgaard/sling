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
import urllib.parse
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

flags.define("--caption",
             default=None,
             help="photo caption")

flags.define("--captionless",
             help="no photo caption",
             default=False,
             action="store_true")

flags.define("--numbering",
             help="photo numbering for albums",
             default=False,
             action="store_true")

flags.define("--source",
             default=None,
             help="photo source")

flags.define("--nsfw",
             help="mark photos as nsfw",
             default=False,
             action="store_true")

flags.define("--overwrite",
             help="overwrite existing photos",
             default=False,
             action="store_true")

flags.define("--remove",
             help="remove photos",
             default=False,
             action="store_true")

flags.define("--truncate",
             help="truncate after first deleted photo",
             default=False,
             action="store_true")

flags.define("--check",
             help="check that photo exists before adding",
             default=False,
             action="store_true")

flags.define("--dryrun",
             help="do not update database",
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
n_stated_in = store["P248"]
n_has_quality = store["P1552"]
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

# Read existing photo profile for item.
updated = False
profile = read_profile(flags.arg.id)
if profile is None:
  profile = store.frame({})

# Get existing set of photo urls.
photos = set()
for media in profile(n_media):
  photos.add(store.resolve(media))
if len(photos) > 0: print(len(photos), "exisiting photos")

# Get API keys for Imgur.
imgurkeys = None
if os.path.exists(flags.arg.imgurkeys):
  with open(flags.arg.imgurkeys, "r") as f:
    imgurkeys = json.load(f)

# Add photo to profile.
def add_photo(profile, url, caption=None, source=None, nsfw=False):
  # Check if photo is already in the profile.
  if url in photos:
    print("Skip existing photo", url)
    return

  # Check if photo exists.
  if flags.arg.check:
    r = session.head(url)
    if r.status_code != 200:
      print("Skip removed photo:", url)
      return

  photos.add(url)
  print("Add", url,
        caption if caption != None else "",
        "NSFW" if nsfw else "")

  # Add media to profile.
  slots = [(n_is, url)]
  if caption and not flags.arg.captionless: slots.append((n_legend, caption))
  if source: slots.append((n_stated_in, store[source]))
  if nsfw: slots.append((n_has_quality, n_nsfw))
  if len(slots) == 1:
    profile.append(n_media, url)
  else:
    frame = store.frame(slots)
    profile.append(n_media, frame)

  # Mark profile as updated.
  global updated
  updated = True

# Add Imgur album.
def add_imgur_album(albumid):
  print("Imgur album", albumid)
  auth = {'Authorization': "Client-ID " + imgurkeys["clientid"]}
  r = session.get("https://api.imgur.com/3/album/" + albumid, headers=auth)
  r.raise_for_status()
  reply = r.json()["data"]
  #print(json.dumps(reply, indent=2))

  serial = 1
  total = len(reply["images"])
  title = reply["title"]
  for image in reply["images"]:
    link = image["link"]

    # Remove query parameters.
    qs = link.find("?")
    if qs != -1: link = link[:qs]

    # Skip anmated GIFs.
    if (image["animated"]):
      print("Skipping animated image", link);
      continue

    # Image caption.
    caption = image["title"]
    if caption is None:
      caption = image["description"]
    if caption is None and title != None:
      if flags.arg.numbering:
        caption = title + " (%d/%d)" % (serial, total)
      else:
        caption = title
    if caption != None:
      caption = caption.replace("\n", " ").strip()

    # NSFW flag.
    nsfw = flags.arg.nsfw or reply["nsfw"] or image["nsfw"]

    # Add media frame to profile.
    add_photo(profile, link, caption, None, nsfw)
    serial += 1

# Add Imgur image.
def add_imgur_image(imageid):
  print("Imgur image", imageid)
  auth = {'Authorization': "Client-ID " + imgurkeys["clientid"]}
  r = session.get("https://api.imgur.com/3/image/" + imageid, headers=auth)
  r.raise_for_status()
  reply = r.json()["data"]

  # Photo URL.
  link = reply["link"]

  # Skip anmated GIFs.
  if (reply["animated"]):
    print("Skipping animated image", link);
    return

  # Image caption.
  caption = reply["title"]
  if caption is None:
    caption = reply["description"]
  if caption != None:
    caption = caption.replace("\n", " ").strip()

  # NSFW flag.
  nsfw = flags.arg.nsfw or reply["nsfw"] or reply["nsfw"]

  # Add media frame to profile.
  add_photo(profile, link, caption, None, nsfw)

# Add Reddit gallery.
def add_reddit_gallery(galleryid, posting=False):
  print("Redit gallery", galleryid)
  r = session.get("https://api.reddit.com/api/info/?id=t3_" + galleryid,
                  headers = {"User-agent": "SLING Bot 1.0"})
  r.raise_for_status()
  reply = r.json()["data"]["children"][0]["data"]
  #print(json.dumps(reply, indent=2))

  mediadata = reply["media_metadata"]
  if mediadata is None:
    print("Skipping empty gallery", galleryid);
    return

  serial = 1
  items = reply["gallery_data"]["items"]
  for item in items:
    mediaid = item["media_id"]
    link = mediadata[mediaid]["s"]["u"]
    m = re.match("https://preview.redd.it/(\w+\.\w+)\?", link)
    if m != None: link = "https://i.redd.it/" + m.group(1)

    # Image caption.
    caption = reply["title"]
    if flags.arg.captionless: caption = None
    if caption != None and flags.arg.numbering:
      caption = "%s (%d/%d)" % (caption, serial, len(items))

    # NSFW flag.
    nsfw = flags.arg.nsfw or reply["over_18"]

    # Add media frame to profile.
    add_photo(profile, link, caption, None, nsfw)
    serial += 1

if flags.arg.overwrite:
  del profile[n_media]
  photos = set()
  updated = True

if flags.arg.remove:
  # Remove media matching urls.
  keep = []
  truncating = False
  for media in profile(n_media):
    link = store.resolve(media)
    if truncating or link in flags.arg.url:
      print("Remove", link)
      photos.remove(link)
      if flags.arg.truncate: truncating = True
    else:
      keep.append((n_media, media))
  del profile[n_media]
  profile.extend(keep)
  updated = True
else:
  # Fetch photo urls.
  for url in flags.arg.url:
    # Imgur album.
    m = re.match("https?://imgur.com/a/(\w+)", url)
    if m != None:
      albumid = m.group(1)
      add_imgur_album(albumid)
      continue

    # Imgur gallery.
    m = re.match("https?://imgur.com/gallery/(\w+)", url)
    if m != None:
      galleryid = m.group(1)
      add_imgur_album(galleryid)
      continue

    # Single-image imgur.
    m = re.match("https?://imgur.com/(\w+)", url)
    if m != None:
      imageid = m.group(1)
      add_imgur_image(imageid)
      continue

    # Reddit gallery.
    m = re.match("https://www.reddit.com/gallery/(\w+)", url)
    if m != None:
      galleryid = m.group(1)
      add_reddit_gallery(galleryid)
      continue

    # Reddit posting.
    m = re.match("https://www.reddit.com/r/\w+/comments/(\w+)/", url)
    if m != None:
      galleryid = m.group(1)
      add_reddit_gallery(galleryid)
      continue

    # DR image scaler.
    m = re.match("https://asset.dr.dk/ImageScaler/\?(.+)", url)
    if m != None:
      print(m.group(1))
      q = urllib.parse.parse_qs(m.group(1))
      url = "https://%s/%s" % (q["server"][0], q["file"][0])

    # Add media to profile.
    add_photo(profile, url, flags.arg.caption, flags.arg.source, flags.arg.nsfw)

# Write profile.
if flags.arg.dryrun:
  print(len(photos), "photos;", flags.arg.id, "not updated")
  print(profile.data(pretty=True))
elif updated:
  print(flags.arg.id, len(photos), "photos")
  store.coalesce()
  write_profile(flags.arg.id, profile)


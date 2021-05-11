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
             default="vault/photo",
             metavar="DB")

flags.define("--imgurkeys",
             default="local/keys/imgur.json",
             help="Imgur API key file")

flags.define("--caption",
             default=None,
             help="photo caption")

flags.define("--fixedcaption",
             default=None,
             help="override photo caption")

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

flags.define("--batch",
             default=None,
             help="batch file for bulk import")

flags.define("url",
             nargs="*",
             help="photo URLs",
             metavar="URL")

flags.parse()

# Sanity Check for (missing) profile id.
if flags.arg.id and flags.arg.id.startswith("http"):
  raise Exception("invalid id: " + flags.arg.id)

photodb = sling.Database(flags.arg.photodb)
session = requests.Session()

store = sling.Store()
n_media = store["media"]
n_is = store["is"]
n_legend = store["P2096"]
n_stated_in = store["P248"]
n_has_quality = store["P1552"]
n_nsfw = store["Q2716583"]

# Get API keys for Imgur.
imgurkeys = None
if os.path.exists(flags.arg.imgurkeys):
  with open(flags.arg.imgurkeys, "r") as f:
    imgurkeys = json.load(f)

# Read item photo profile from database.
def read_profile(itemid):
  if itemid is None: return None
  data = photodb[itemid]
  if data is None: return None
  profile = store.parse(data)
  return profile

# Write item photo profile to database.
def write_profile(itemid, profile):
  data = profile.data(binary=True)
  photodb[itemid] = data

# Add photo to profile.
def add_photo(profile, url, caption=None, source=None, nsfw=False):
  # Check if photo exists.
  if flags.arg.check:
    r = session.head(url)
    if r.status_code // 100 == 3:
      redirect = r.headers['Location']
      if redirect.endswith("/removed.png"):
        print("Skip removed photo:", url, r.status_code)
        return 0

      # Check if redirect exists.
      r = session.head(redirect)
      if r.status_code != 200:
        print("Skip missing redirect:", url, r.status_code)
        return 0

      # Use redirected url.
      url = redirect
    elif r.status_code != 200:
      print("Skip missing photo:", url, r.status_code)
      return 0

    # Check content type.
    ct = r.headers.get("Content-Type")
    if ct != None and not ct.startswith("image/"):
      print("Skip non-image content:", url, ct)
      return 0

  # Check if photo is already in the profile.
  alturl = None
  if url.startswith("https://imgur.com/"):
    alturl = "https://i.imgur.com/" + url[18:]
  elif url.startswith("https://i.imgur.com/"):
    alturl = "https://imgur.com/" + url[20:]
  for media in profile(n_media):
    media = store.resolve(media)
    if media == url or media == alturl:
      print("Skip existing photo", url)
      return 0

  print("Add", url,
        caption if caption != None else "",
        "NSFW" if nsfw else "")

  # Add media to profile.
  slots = [(n_is, url)]
  if flags.arg.fixedcaption: caption = flags.arg.fixedcaption
  if caption and not flags.arg.captionless: slots.append((n_legend, caption))
  if source: slots.append((n_stated_in, store[source]))
  if nsfw: slots.append((n_has_quality, n_nsfw))
  if len(slots) == 1:
    profile.append(n_media, url)
  else:
    frame = store.frame(slots)
    profile.append(n_media, frame)

  return 1

# Add Imgur album.
def add_imgur_album(profile, albumid, isnsfw=False):
  print("Imgur album", albumid)
  auth = {'Authorization': "Client-ID " + imgurkeys["clientid"]}
  r = session.get("https://api.imgur.com/3/album/" + albumid, headers=auth)
  if r.status_code == 404:
    print("Skipping missing album", albumid)
    return 0
  if r.status_code == 403:
    print("Skipping unaccessible album", albumid)
    return 0
  r.raise_for_status()
  reply = r.json()["data"]
  #print(json.dumps(reply, indent=2))

  serial = 1
  total = len(reply["images"])
  title = reply["title"]
  count = 0
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
    nsfw = isnsfw or reply["nsfw"] or image["nsfw"]

    # Add media frame to profile.
    if add_photo(profile, link, caption, None, nsfw): count += 1
    serial += 1
  return count

# Add Imgur image.
def add_imgur_image(profile, imageid, isnsfw=False):
  print("Imgur image", imageid)
  auth = {'Authorization': "Client-ID " + imgurkeys["clientid"]}
  r = session.get("https://api.imgur.com/3/image/" + imageid, headers=auth)
  if r.status_code == 404:
    print("Skipping missing image", imageid)
    return 0
  r.raise_for_status()
  reply = r.json()["data"]
  #print(json.dumps(reply, indent=2))

  # Photo URL.
  link = reply["link"]
  qs = link.find("?")
  if qs != -1: link = link[:qs]

  # Skip anmated GIFs.
  if (reply["animated"]):
    print("Skipping animated image", link);
    return 0

  # Image caption.
  caption = reply["title"]
  if caption is None:
    caption = reply["description"]
  if caption != None:
    caption = caption.replace("\n", " ").strip()

  # NSFW flag.
  nsfw = isnsfw or reply["nsfw"] or reply["nsfw"]

  # Add media frame to profile.
  return add_photo(profile, link, caption, None, nsfw)

# Add Reddit gallery.
def add_reddit_gallery(profile, galleryid, isnsfw=False):
  print("Redit posting", galleryid)
  r = session.get("https://api.reddit.com/api/info/?id=t3_" + galleryid,
                  headers = {"User-agent": "SLING Bot 1.0"})
  r.raise_for_status()
  children = r.json()["data"]["children"]
  if len(children) == 0:
    print("Skipping empty post", galleryid);
    return 0
  reply = children[0]["data"]
  #print(json.dumps(reply, indent=2))

  if reply["is_self"]:
    print("Skipping self post", galleryid);
    return 0
  removed = reply["removed_by_category"]
  if removed == "deleted" or removed == "moderator":
    print("Skipping deleted post", galleryid);
    return 0

  if reply["is_video"]:
    print("Skipping video", galleryid);
    return 0

  mediadata = reply.get("media_metadata")
  if mediadata is None:
    url = reply.get("url")
    if url is None:
      print("Skipping empty gallery", galleryid);
      return 0

    # Single image in Reddit posting.
    caption = reply["title"]
    if flags.arg.captionless: caption = None
    nsfw = isnsfw or reply["over_18"]

    # Add media frame to profile.
    return add_media(profile, url, caption, nsfw)

  items = reply["gallery_data"]["items"]
  count = 0
  serial = 1
  for item in items:
    mediaid = item["media_id"]
    media = mediadata[mediaid]["s"]
    link = media.get("u")
    if link is None:
      print("Skipping missing image in gallery", mediaid);
      return 0

    m = re.match("https://preview.redd.it/(\w+\.\w+)\?", link)
    if m != None: link = "https://i.redd.it/" + m.group(1)

    # Image caption.
    caption = reply["title"]
    if flags.arg.captionless: caption = None
    if caption != None and flags.arg.numbering:
      caption = "%s (%d/%d)" % (caption, serial, len(items))

    # NSFW flag.
    nsfw = isnsfw or reply["over_18"]

    # Add media frame to profile.
    if add_photo(profile, link, caption, None, nsfw): count += 1
    serial += 1
  return count

# Add media.
def add_media(profile, url, caption, nsfw):
  # Trim url.
  url = url.replace("/i.imgur.com/", "/imgur.com/")
  url = url.replace("/www.imgur.com/", "/imgur.com/")
  url = url.replace("/m.imgur.com/", "/imgur.com/")

  url = url.replace("/www.reddit.com/", "/reddit.com/")
  url = url.replace("/old.reddit.com/", "/reddit.com/")

  if url.startswith("http://reddit.com"): url = "https" + url[4:]
  if url.startswith("http://imgur.com"): url = "https" + url[4:]

  m = re.match("(https://imgur\.com/.+)[\?#].*", url)
  if m == None: m = re.match("(https?://reddit\.com/.+)[\?#].*", url)
  if m == None: m = re.match("(https?://i\.redditmedia\.com/.+)[\?#].*", url)
  if m != None:
    url = m.group(1)
    if url.endswith("/new"): url = url[:-4]

  m = re.match("(https://imgur\.com/.+\.jpe?g)-\w+", url)
  if m != None: url = m.group(1)

  # Discard videos.
  if url.endswith(".gif") or \
     url.endswith(".gifv") or \
     url.endswith(".mp4") or \
     url.endswith(".webm") or \
     url.startswith("https://gfycat.com/") or \
     url.startswith("https://redgifs.com/") or \
     url.startswith("https://v.redd.it/"):
    print("Skipping video", url)
    return 0

  # Imgur album.
  m = re.match("https://imgur\.com/a/(\w+)", url)
  if m != None:
    albumid = m.group(1)
    return add_imgur_album(profile, albumid, nsfw)

  # Imgur gallery.
  m = re.match("https://imgur\.com/gallery/(\w+)", url)
  if m != None:
    galleryid = m.group(1)
    return  add_imgur_album(profile, galleryid, nsfw)
  m = re.match("https?://imgur\.com/\w/\w+/(\w+)", url)
  if m != None:
    galleryid = m.group(1)
    return  add_imgur_album(profile, galleryid, nsfw)

  # Single-image imgur.
  m = re.match("https://imgur\.com/(\w+)$", url)
  if m != None:
    imageid = m.group(1)
    return add_imgur_image(profile, imageid, nsfw)

  # Reddit gallery.
  m = re.match("https://reddit\.com/gallery/(\w+)", url)
  if m != None:
    galleryid = m.group(1)
    return add_reddit_gallery(profile, galleryid, nsfw)

  # Reddit posting.
  m = re.match("https://reddit\.com/r/\w+/comments/(\w+)/", url)
  if m != None:
    galleryid = m.group(1)
    return add_reddit_gallery(profile, galleryid, nsfw)

  # DR image scaler.
  m = re.match("https://asset.dr.dk/ImageScaler/\?(.+)", url)
  if m != None:
    print(m.group(1))
    q = urllib.parse.parse_qs(m.group(1))
    url = "https://%s/%s" % (q["server"][0], q["file"][0])

  # Add media to profile.
  return add_photo(profile, url, caption, flags.arg.source, nsfw)

# Bulk load photos from batch file.
def bulk_load(batch):
  profiles = {}
  updated = set()
  fin = open(batch)
  num_new = 0
  num_photos = 0
  for line in fin:
    # Get id, url, and nsfw fields.
    tab = line.find('\t')
    if tab != -1: line = line[tab + 1:].strip()
    line = line.strip()
    if len(line) == 0: continue
    fields = line.split()
    id = fields[0]
    url = fields[1]
    nsfw = len(fields) >= 3 and fields[2] == "NSFW"

    # Get profile or create a new one.
    profile = profiles.get(id)
    if profile is None:
      profile = read_profile(id)
      if profile is None:
        profile = store.frame({})
        num_new += 1
      profiles[id] = profile
      print("*** PROFILE %s, %d existing photos ***************************" %
            (id, profile.count(n_media)))

    # Add media to profile
    n = add_media(profile, url, flags.arg.caption, nsfw or flags.arg.nsfw)
    if n > 0:
      num_photos += n
      updated.add(id)

  fin.close()

  # Write updated profiles.
  store.coalesce()
  for id in updated:
    profile = profiles[id]
    if flags.arg.dryrun:
      print(profile.count(n_media), "photos;", id, "not updated")
      print(profile.data(pretty=True))
    elif updated:
      print("Write", id, profile.count(n_media), "photos")
      write_profile(id, profile)

  print(len(profiles), "profiles,",
        num_new, "new,",
        len(updated), "updated,",
        num_photos, "photos")

if flags.arg.batch:
  # Bulk import.
  bulk_load(flags.arg.batch)
else:
  # Read existing photo profile for item.
  profile = read_profile(flags.arg.id)
  updated = False
  if profile is None:
    profile = store.frame({})
  else:
    print(profile.count(n_media), "exisiting photos")

  # Delete all existing maedia on overwrite mode.
  if flags.arg.overwrite:
    del profile[n_media]
    updated = True

  if flags.arg.remove:
    # Remove media matching urls.
    keep = []
    truncating = False
    for media in profile(n_media):
      link = store.resolve(media)
      if truncating or link in flags.arg.url:
        print("Remove", link)
        if flags.arg.truncate: truncating = True
        updated = True
      else:
        keep.append((n_media, media))
    del profile[n_media]
    profile.extend(keep)
  else:
    # Fetch photo urls.
    for url in flags.arg.url:
      if add_media(profile, url, flags.arg.caption, flags.arg.nsfw):
        updated = True

  # Write profile.
  if flags.arg.dryrun:
    print(profile.count(n_media), "photos;", flags.arg.id, "not updated")
    print(profile.data(pretty=True))
  elif updated:
    print("Write", flags.arg.id, profile.count(n_media), "photos")
    store.coalesce()
    write_profile(flags.arg.id, profile)


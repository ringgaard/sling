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

"""Photo profile library."""

import hashlib
import json
import os
import re
import requests
import urllib.parse

import sling
import sling.flags as flags

flags.define("--photodb",
             help="database for photo profiles",
             default="vault/photo",
             metavar="DB")

flags.define("--mediadb",
             help="database for images",
             default="vault/media",
             metavar="DB")

flags.define("--check",
             help="check that photo exists before adding",
             default=False,
             action="store_true")

flags.define("--fixedcaption",
             default=None,
             help="override photo caption")

flags.define("--captionless",
             help="no photo caption",
             default=False,
             action="store_true")

flags.define("--perimagecaption",
             help="use individual captions for images in albums",
             default=False,
             action="store_true")

flags.define("--numbering",
             help="photo numbering for albums",
             default=False,
             action="store_true")

flags.define("--source",
             default=None,
             help="photo source")

flags.define("--video",
             help="allow video clips",
             default=False,
             action="store_true")

flags.define("--albums",
             help="add albums from posting",
             default=False,
             action="store_true")

# Photo database.
db = None

# Media database.
mediadb = None

# Session for fetching image data.
session = requests.Session()

# Fingerprints for bad photos.
bad_photos = set([
  b'\xf1{\x01\x90\x1cu,\x1b\xb0I(\x13\x1d\x16a\xaf', # i.reddit.it
  b'\xd85\x88Cs\xf4\xd6\xc8\xf2GB\xce\xab\xe7IF',    # i.imgur.com/removed.png
])

# Global store.
store = sling.Store()
n_media = store["media"]
n_is = store["is"]
n_legend = store["P2096"]
n_stated_in = store["P248"]
n_has_quality = store["P1552"]
n_nsfw = store["Q2716583"]

# Get API keys for Imgur.
imgurkeys = None
if os.path.exists("local/keys/imgur.json"):
  with open("local/keys/imgur.json", "r") as f:
    imgurkeys = json.load(f)

def photodb():
  global db
  if db is None: db = sling.Database(flags.arg.photodb, "photo.py")
  return db

class Profile:
  def __init__(self, itemid):
    self.itemid = itemid
    self.excluded = None
    self.isnew = False
    data, _ = photodb().get(itemid)
    if data is None:
      self.frame = store.frame({})
      self.isnew = True
    else:
      self.frame = store.parse(data)

  # Write photo profile to database.
  def write(self):
    if self.itemid is None or self.itemid == "": raise Error("empty id")
    data = self.frame.data(binary=True)
    photodb().put(self.itemid, data)

  # Clear all photos.
  def clear(self):
    del self.frame[n_media]

  # Return number of photos in profile.
  def count(self):
    return self.frame.count(n_media)

  # Return iterator over all photos.
  def media(self):
    return self.frame(n_media)

  # Return url for media.
  def url(self, media):
    if type(media) is sling.Frame: return media[n_is]
    return media

  # Return caption for photo.
  def caption(self, media):
    if type(media) is sling.Frame:
      return str(media[n_caption])
    else:
      return None

  # Import all photos from another profile.
  def copy(self, other):
    num_added = 0
    for media in other.media():
      if type(media) is sling.Frame:
        url = media[n_is]
        caption = media[n_legend]
        nsfw = media[n_has_quality] == n_nsfw
        num_added += self.add_media(url, caption, nsfw)
      else:
        num_added += self.add_media(media, None, False)
    return num_added

  # Replace photos in profile:
  def replace(self, photos):
    slots = []
    for photo in photos: slots.append((n_media, photo))
    del self.frame[n_media]
    self.frame.extend(slots)

  # Check if photo already in profile.
  def has(self, url, alturl=None):
    for media in self.media():
      if type(media) is sling.Frame: media = media[n_is]
      if media == url or media == alturl: return True
    return False

  # Add photo to profile.
  def add_photo(self, url, caption=None, source=None, nsfw=False):
    # Check if photo should be excluded.
    if self.excluded and url in self.excluded:
      print("Skip excluded photo:", url)
      return 0

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

      # Check content length.
      if url.startswith("https://i.reddituploads.com/"):
        length = r.headers.get("Content-Length")
        if length == 0:
          print("Skip empty photo:", url)
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
    if self.has(url, alturl):
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
      self.frame.append(n_media, url)
    else:
      self.frame.append(n_media, store.frame(slots))

    return 1

  # Add Imgur album.
  def add_imgur_album(self, albumid, caption, isnsfw=False):
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
    album_title = reply["title"]
    if album_title is None:
       album_title = caption
    elif caption is not None and caption.startswith(album_title):
      album_title = caption

    count = 0
    for image in reply["images"]:
      link = image["link"]

      # Remove query parameters.
      qs = link.find("?")
      if qs != -1: link = link[:qs]

      # Skip anmated GIFs.
      if (not flags.arg.video and image["animated"]):
        print("Skipping animated image", link);
        continue

      # Image caption.
      if flags.arg.perimagecaption:
        title = image["title"]
        if title is None:
          title = image["description"]
      else:
        title = None

      if title is None and album_title != None:
        if flags.arg.numbering:
          title = album_title + " (%d/%d)" % (serial, total)
        else:
          title = album_title
      if title != None:
        title = title.replace("\n", " ").strip()

      # NSFW flag.
      nsfw = isnsfw or reply["nsfw"] or image["nsfw"]

      # Add media frame to profile.
      if self.add_photo(link, title, None, nsfw): count += 1
      serial += 1
    return count

  # Add Imgur image.
  def add_imgur_image(self, imageid, isnsfw=False):
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
    if (not flags.arg.video and reply["animated"]):
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

    # Add media to profile frame.
    return self.add_photo(link, caption, None, nsfw)

  # Add Reddit gallery.
  def add_reddit_gallery(self, galleryid, caption, isnsfw=False):
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

    if not flags.arg.albums and reply["is_self"]:
      print("Skipping self post", galleryid);
      return 0
    if reply["removed_by_category"] != None:
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

      title = reply["title"]
      if title is None: title = caption
      if flags.arg.captionless: title = None
      nsfw = isnsfw or reply["over_18"]

      count = 0
      if flags.arg.albums:
        # Fetch albums from text.
        selftext = reply["selftext"]
        for m in re.finditer("\[(.+)\]\((https?://imgur.com/a/\w+)\)", selftext):
          print("Add album", m[2], m[1])
          count += self.add_media(m[2], m[1], nsfw)
      else:
        # Add media to profile frame.
        count = self.add_media(url, title, nsfw)

      return count

    # Get gallery items.
    gallery = reply.get("gallery_data")
    if gallery is None:
      print("Skipping missing gallery data in", galleryid);
      return 0
    items = gallery.get("items")
    if items is None:
      print("Skipping missing gallery items in", galleryid);
      return 0

    count = 0
    serial = 1
    for item in items:
      mediaid = item["media_id"]
      media = mediadata[mediaid].get("s")
      if media is None:
        print("Skipping missing media in gallery", mediaid);
        continue
      link = media.get("u")
      if link is None:
        print("Skipping missing image in gallery", mediaid);
        continue

      m = re.match("https://preview.redd.it/(\w+\.\w+)\?", link)
      if m != None: link = "https://i.redd.it/" + m.group(1)

      # Image caption.
      title = reply["title"]
      if title is None:
        title = caption
      elif caption is not None and caption.startswith(title):
        title = caption
      if flags.arg.captionless: title = None

      if title != None and flags.arg.numbering:
        title = "%s (%d/%d)" % (title, serial, len(items))

      # NSFW flag.
      nsfw = isnsfw or reply["over_18"]

      # Add media to profile frame.
      if self.add_photo(link, title, None, nsfw): count += 1
      serial += 1
    return count

  # Add media.
  def add_media(self, url, caption, nsfw):
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
    if m == None: m = re.match("(https?://i\.redd\.it/.+)[\?#].*", url)
    if m == None: m = re.match("(https?://i\.redditmedia\.com/.+)[\?#].*", url)
    if m != None:
      url = m.group(1)
      if url.endswith("/new"): url = url[:-4]

    m = re.match("(https://imgur\.com/.+\.jpe?g)-\w+", url)
    if m != None: url = m.group(1)

    # Discard videos.
    if not flags.arg.video:
      if url.endswith(".gif") or \
         url.endswith(".gifv") or \
         url.endswith(".mp4") or \
         url.endswith(".webm") or \
         url.startswith("https://gfycat.com/") or \
         url.startswith("https://redgifs.com/") or \
         url.startswith("https://v.redd.it/"):
        print("Skipping video", url)
        return 0

    # Discard empty urls.
    if len(url) == 0: return 0

    # Imgur album.
    m = re.match("https://imgur\.com/a/(\w+)", url)
    if m != None:
      albumid = m.group(1)
      return self.add_imgur_album(albumid, caption, nsfw)

    # Imgur gallery.
    m = re.match("https://imgur\.com/gallery/(\w+)", url)
    if m != None:
      galleryid = m.group(1)
      return  self.add_imgur_album(galleryid, caption, nsfw)
    m = re.match("https?://imgur\.com/\w/\w+/(\w+)", url)
    if m != None:
      galleryid = m.group(1)
      return  self.add_imgur_album(galleryid, caption, nsfw)

    # Single-image imgur.
    m = re.match("https://imgur\.com/(\w+)$", url)
    if m != None:
      imageid = m.group(1)
      return self.add_imgur_image(imageid, nsfw)

    # Reddit gallery.
    m = re.match("https://reddit\.com/gallery/(\w+)", url)
    if m != None:
      galleryid = m.group(1)
      return self.add_reddit_gallery(galleryid, caption, nsfw)

    # Reddit posting.
    m = re.match("https://reddit\.com/r/\w+/comments/(\w+)/", url)
    if m != None:
      galleryid = m.group(1)
      return self.add_reddit_gallery(galleryid, caption, nsfw)

    # Reddit preview.
    m = re.match("https://preview.redd.it/(\w+.png)\?", url)
    if m != None:
      imagename = m.group(1)
      url = "https://i.redd.it/" + imagename

    # DR image scaler.
    m = re.match("https://asset.dr.dk/ImageScaler/\?(.+)", url)
    if m != None:
      q = urllib.parse.parse_qs(m.group(1))
      url = "https://%s/%s" % (q["server"][0], q["file"][0])

    # Add media to profile.
    return self.add_photo(url, caption, flags.arg.source, nsfw)

  def dedup(self):
    # Connect to media database.
    global mediadb
    if mediadb is None: mediadb = sling.Database(flags.arg.mediadb, "photo.py")

    # Compute image hash for each photo to detect duplicates.
    fingerprints = {}
    duplicates = set()
    missing = set()
    naughty = set()
    num_photos = 0
    num_duplicates = 0
    num_missing = 0
    for media in self.media():
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
            print(self.itemid, url, " nsfw duplicate of", dup)
          else:
            print(self.itemid, url, " duplicate of", dup)
        else:
          if dup in naughty:
            print(self.itemid, url, " sfw duplicate of", dup)
          else:
            print(self.itemid, url, " duplicate of", dup)
      elif fingerprint in bad_photos:
        missing.add(url)
        num_missing += 1
        print(self.itemid, url, " bad")
      else:
        fingerprints[fingerprint] = url

      if nsfw: naughty.add(url)
      num_photos += 1

    print(self.itemid,
        num_photos, "photos,",
        num_duplicates, "duplicates",
        num_missing, "missing")

    # Remove duplicates.
    if num_duplicates > 0 or num_missing > 0:
      # Find photos to keep.
      keep = []
      for media in self.media():
        url = store.resolve(media)
        if url not in duplicates and url not in missing: keep.append(media)
      self.replace(keep)

    return num_duplicates + num_missing


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
import io
import re
import requests
import ssl
import urllib.parse
import urllib3

import imagehash
from PIL import Image

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

flags.define("--fpdb",
             help="database for photo fingerprints",
             default="vault/fingerprint",
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

flags.define("--preservecaptioned",
             help="do not remove captioned duplicates",
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

flags.define("--hash",
             help="image hash method",
             default="md5")

# Photo database.
db = None

# Media database.
mediadb = None

# Photo fingerprint database.
fpdb = None

# Session for fetching image data. Disable SSL checking.
class TLSAdapter(requests.adapters.HTTPAdapter):
  def init_poolmanager(self, *args, **kwargs):
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.set_ciphers('DEFAULT@SECLEVEL=1')
    kwargs['ssl_context'] = ctx
    return super(TLSAdapter, self).init_poolmanager(*args, **kwargs)

session = requests.Session()
session.verify = False
session.mount('https://', TLSAdapter())
urllib3.disable_warnings()

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

# Tri-state override.
def tri(value, override):
  if override is None:
    return value
  else:
    return override

def photodb():
  global db
  if db is None: db = sling.Database(flags.arg.photodb)
  return db

# Video detection.

video_suffixes = [
  ".gifv",
  ".mp4",
  ".webm",
]

video_prefixes = [
  "https://gfycat.com/",
  "https://redgifs.com/",
  "https://v.redd.it/"
]

def is_video(url):
  for suffix in video_suffixes:
    if url.endswith(suffix): return True
  for prefix in video_prefixes:
    if url.startswith(suffix): return True
  return False

# Image hashing.

Image.MAX_IMAGE_PIXELS = None

def md5_hasher(image):
  return hashlib.md5(image).hexdigest(), 1, len(image)

def img_hash(image, hasher):
  try:
    img = Image.open(io.BytesIO(image))
    return str(hasher(img)), img.width, img.height
  except Exception:
    return md5_hasher(image)

def average_hasher(image):
  return img_hash(image, imagehash.average_hash)

def perceptual_hasher(image):
  return img_hash(image, imagehash.phash)

def difference_hasher(image):
  return img_hash(image, imagehash.dhash)

def wavelet_hasher(image):
  return img_hash(image, imagehash.whash)

def color_hasher(image):
  return img_hash(image, imagehash.colorhash)

def crop_hasher(image):
  return img_hash(image, imagehash.crop_resistant_hash)

def avg16_hasher(image):
  return img_hash(image, lambda img: imagehash.average_hash(image, 16))

image_hashers = {
  "md5": md5_hasher,
  "average": average_hasher,
  "perceptual": perceptual_hasher,
  "difference": difference_hasher,
  "wavelet": wavelet_hasher,
  "color": color_hasher,
  "crop": crop_hasher,
  "avg16": avg16_hasher,
}

class Photo:
  def __init__(self, item, url):
    self.item = item
    self.url = url
    self.fingerprint = None
    self.width = None
    self.height = None

  def size(self):
    return self.width * self.height

photo_cache = {}

def fetch_image(url):
  # Try to get image from media database.
  global mediadb
  if mediadb is None and flags.arg.mediadb:
    mediadb = sling.Database(flags.arg.mediadb)
  if mediadb:
    image = mediadb[url]
    if image:
      print("photodb", url)
      return image

  # Fetch image from source if it is not in the media database.
  r = session.get(url)
  if r.status_code != 200: return None
  print("fetch", url)
  return r.content

def get_photo(item, url):
  # Check if photo is cached.
  photo = photo_cache.get(url)
  if photo is not None: return photo

  # Get image.
  image = fetch_image(url)
  if image is None: return None

  # Get photo fingerprint.
  photo = Photo(item, url)
  hasher = image_hashers[flags.arg.hash]
  photo.fingerprint, photo.width, photo.height = hasher(image)

  # Add photo to cache.
  photo_cache[url] = photo

  return photo

def load_fingerprints(urls):
  missing = []
  for url in urls:
    if url not in photo_cache: missing.append(url)
  if len(missing) == 0: return

  # Fetch fingerprints from database.
  if len(flags.arg.fpdb) == 0: return
  global fpdb
  if fpdb is None: fpdb = sling.Database(flags.arg.fpdb)

  for url, data in fpdb[missing].items():
    if data is None: continue
    fp = json.loads(data)
    if flags.arg.hash not in fp: continue

    photo = Photo(fp.get("item"), url)
    photo.fingerprint = fp[flags.arg.hash]
    photo.width = fp["width"]
    photo.height = fp["height"]
    photo_cache[url] = photo

class Profile:
  def __init__(self, itemid, data=None):
    self.itemid = itemid
    self.excluded = None
    self.isnew = False
    self.skipdups = True
    self.captionless = flags.arg.captionless
    if data is None and itemid is not None:
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
    result = photodb().put(self.itemid, data)
    if result == sling.DBUNCHANGED: print(self.itemid, "unchanged")

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
      return str(media[n_legend])
    else:
      return None

  # Check if media has caption.
  def captioned(self, media):
    if type(media) is sling.Frame:
      return n_legend in media
    else:
      return False

  # Preload photo fingerprints.
  def preload_fingerprints(self):
    urls = []
    for media in self.media():
      if type(media) is sling.Frame: media = media[n_is]
      if is_video(media): continue
      urls.append(media)
    load_fingerprints(urls)

  # Import all photos from another profile.
  def copy(self, other):
    num_added = 0
    for media in other.media():
      if type(media) is sling.Frame:
        url = media[n_is]
        caption = media[n_legend]
        nsfw = media[n_has_quality] == n_nsfw
        num_added += self.add_photo(url, caption, None, nsfw)
      else:
        num_added += self.add_photo(media, None, None, False)
    return num_added

  # Replace photos in profile:
  def replace(self, photos):
    slots = []
    for photo in photos: slots.append((n_media, photo))
    del self.frame[n_media]
    self.frame.extend(slots)

  # Return all media urls.
  def urls(self):
    urls = []
    for media in self.media():
      if type(media) is sling.Frame: media = media[n_is]
      urls.append(media)
    return urls

  # Check if photo already in profile.
  def has(self, url, alturl=None):
    for media in self.media():
      if type(media) is sling.Frame: media = media[n_is]
      if media == url or media == alturl: return True
    return False

  # Add photo to profile.
  def add_photo(self, url, caption=None, source=None, nsfw=None):
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
    if self.skipdups and self.has(url, alturl):
      print("Skip existing photo", url)
      return 0

    #print("Photo", url,
    #      caption if caption != None else "",
    #      "NSFW" if nsfw else "")

    # Add media to profile.
    slots = [(n_is, url)]
    if flags.arg.fixedcaption: caption = flags.arg.fixedcaption
    if caption and not self.captionless: slots.append((n_legend, caption))
    if source: slots.append((n_stated_in, store[source]))
    if nsfw: slots.append((n_has_quality, n_nsfw))
    if len(slots) == 1:
      self.frame.append(n_media, url)
    else:
      self.frame.append(n_media, store.frame(slots))

    return 1

  # Add Imgur album.
  def add_imgur_album(self, albumid, caption, nsfw_override=None):
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
      nsfw = tri(reply["nsfw"] or image["nsfw"], nsfw_override)

      # Add media frame to profile.
      if self.add_photo(link, title, None, nsfw): count += 1
      serial += 1
    return count

  # Add Imgur image.
  def add_imgur_image(self, imageid, nsfw_override=None):
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
    nsfw = tri(reply["nsfw"] or reply["nsfw"], nsfw_override)

    # Add media to profile frame.
    return self.add_photo(link, caption, None, nsfw)

  # Add Reddit gallery.
  def add_reddit_gallery(self, galleryid, caption, nsfw_override=None):
    print("Redit posting", galleryid)
    r = requests.get("https://api.reddit.com/api/info/?id=t3_" + galleryid,
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
      if self.captionless: title = None
      nsfw = tri(reply["over_18"], nsfw_override)

      count = 0
      if flags.arg.albums:
        # Fetch albums from text.
        selftext = reply["selftext"]
        for m in re.finditer("\[(.+)\]\((https?://imgur.com/a/\w+)\)", selftext):
          print("Album", m[2], m[1])
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
      if self.captionless: title = None

      if title != None and flags.arg.numbering:
        title = "%s (%d/%d)" % (title, serial, len(items))

      # NSFW flag.
      nsfw = tri(reply["over_18"], nsfw_override)

      # Add media to profile frame.
      if self.add_photo(link, title, None, nsfw): count += 1
      serial += 1
    return count

  # Add media.
  def add_media(self, url, caption=None, nsfw=None):
    # Trim url.
    url = url.replace("/i.imgur.com/", "/imgur.com/")
    url = url.replace("/www.imgur.com/", "/imgur.com/")
    url = url.replace("/m.imgur.com/", "/imgur.com/")

    m = re.match("(https://imgur\.com/.+)\.jpeg", url)
    if m != None: url = m.group(1) + ".jpg"

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

    m = re.match("https://preview.redd.it/(\w+.png)\?.+", url)
    if m != None: url = "https://i.redd.it/" + m.group(1)

    m = re.match("(https://imgur\.com/.+\.jpe?g)-\w+", url)
    if m != None: url = m.group(1)

    # Discard videos.
    if not flags.arg.video and is_video(url):
      print("Skipping video", url)
      return 0

    # Discard subreddits.
    m = re.match("^https?://reddit\.com/r/\w+/?$", url)
    if m != None: return 0;

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
    m = re.match("https://(www\.)?reddit\.com/\w+/.+/comments/(\w+)/", url)
    if m != None:
      galleryid = m.group(2)
      return self.add_reddit_gallery(galleryid, caption, nsfw)

    # Reddit preview.
    m = re.match("https://preview.redd.it/(\w+.png)\?", url)
    if m != None:
      imagename = m.group(1)
      url = "https://i.redd.it/" + imagename
    m = re.match("https://preview.redd.it/(\w+.jpg)\?", url)
    if m != None:
      imagename = m.group(1)
      url = "https://i.redd.it/" + imagename

    # DR image scaler.
    m = re.match("https://asset.dr.dk/[Ii]mage[Ss]caler/\?(.+)", url)
    if m != None:
      q = urllib.parse.parse_qs(m.group(1))
      url = "https://%s/%s" % (q["server"][0], q["file"][0])

    # Add media to profile.
    return self.add_photo(url, caption, flags.arg.source, nsfw)

  def dedup(self):
    # Add photo fingerprints to cache.
    self.preload_fingerprints()

    # Compute image hash for each photo to detect duplicates.
    photos = {}
    pixels = {}
    captions = {}
    duplicates = set()
    missing = set()
    naughty = set()
    num_photos = 0
    for media in self.media():
      url = store.resolve(media)
      if is_video(url): continue
      nsfw = type(media) is sling.Frame and media[n_has_quality] == n_nsfw
      captioned = type(media) is sling.Frame and n_legend in media
      if captioned: captions[url] = media[n_legend]

      # Get photo information.
      photo = get_photo(self.itemid, url)
      if photo is None:
        missing.add(url)
        continue

      # Check for duplicate.
      dup = photos.get(photo.fingerprint)
      photos[photo.fingerprint] = photo

      if dup != None:
        if flags.arg.preservecaptioned and captioned:
          print(self.itemid, url, " preserve captioned duplicate of", dup.url)
        else:
          # Keep photo with the longest caption or the most pixels.
          caption = captions.get(url, "")
          dupcaption = captions.get(dup, "")
          bigger = photo.size() * 0.8 > dup.size()
          if len(dupcaption) < len(caption) or bigger:
            # Remove previous duplicate.
            duplicates.add(dup.url)
            msg = "duplicate of"
          else:
            # Remove this duplicate.
            duplicates.add(photo.url)

            if nsfw:
              if dup not in naughty:
                msg = "nsfw duplicate of"
              else:
                msg = "duplicate of"
            else:
              if dup in naughty:
                msg = "sfw duplicate of"
              else:
                msg = "duplicate of"

          if len(caption) > len(dupcaption):
            msg = "captioned " + msg
          elif len(caption) < len(dupcaption):
            msg = msg + " captioned"

          if photo.size() < dup.size():
            msg = "smaller " + msg
          elif photo.size() > dup.size():
            msg = "bigger " + msg

          print(self.itemid, url, msg, dup.url)
      elif photo.fingerprint in bad_photos:
        missing.add(photo)
        print(self.itemid, photo.url, " bad")

      if nsfw: naughty.add(photo)
      num_photos += 1

    print(self.itemid,
        num_photos, "photos,",
        len(duplicates), "duplicates",
        len(missing), "missing")

    # Remove duplicates.
    if len(duplicates) > 0 or len(missing) > 0:
      # Find photos to keep.
      keep = []
      for media in self.media():
        url = store.resolve(media)
        if url not in duplicates and url not in missing: keep.append(media)
      self.replace(keep)

    return len(duplicates) + len(missing)


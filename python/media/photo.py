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
import ssl
import time
import urllib.parse
import urllib3

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

flags.define("--captions",
             help="add caption to photos",
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
             default="average")

# Photo database.
db = None

# Media database.
mediadb = None

# Photo fingerprint database.
fpdb = None

urllib3.disable_warnings()
pool =  urllib3.PoolManager(cert_reqs=ssl.CERT_NONE)

# Global store.
store = sling.Store()
n_media = store["media"]
n_is = store["is"]
n_legend = store["P2096"]
n_stated_in = store["P248"]
n_has_quality = store["P1552"]
n_nsfw = store["Q2716583"]

# Get API keys for Imgur and ImgChest.
imgurkeys = None
if os.path.exists("local/keys/imgur.json"):
  with open("local/keys/imgur.json", "r") as f:
    imgurkeys = json.load(f)
imgchestkeys = None
if os.path.exists("local/keys/imgchest.json"):
  with open("local/keys/imgchest.json", "r") as f:
    imgchestkeys = json.load(f)

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

def fingerprintdb():
  global fpdb
  if fpdb is None: fpdb = sling.Database(flags.arg.fpdb)
  return fpdb

# Video detection.

video_suffixes = [
  ".gif",
  ".gifv",
  ".mp4",
  ".webm",
]

video_prefixes = [
  "https://gfycat.com/",
  "https://redgifs.com/",
  "https://v.redd.it/"
]

album_patterns = [
  re.compile(r"\[(.+)\]\((https?:\/\/imgur.com\/a\/\w+)\)"),
  re.compile(r"\[(.+)\]\((https?:\/\/(?:www\.)?imgchest\.com\/p\/\w+)\)"),
  re.compile(r"(https?:\/\/imgur\.com\/a\/\w+)"),
  re.compile(r"(https?:\/\/(?:www\.)?imgchest\.com\/p\/\w+)"),
]

commons_base_url = "https://upload.wikimedia.org/wikipedia/commons"

def is_video(url):
  for suffix in video_suffixes:
    if url.endswith(suffix): return True
  for prefix in video_prefixes:
    if url.startswith(prefix): return True
  return False

def commons_media(fn):
  if fn is None: return None
  fn = fn.replace(' ', '_')
  md5 = hashlib.md5(fn.encode("utf8")).hexdigest()
  fn = fn.replace("?", "%3F")
  fn = fn.replace("+", "%2B")
  fn = fn.replace("&", "%26")
  return "%s/%s/%s/%s" % (commons_base_url, md5[0], md5[0:2], fn)

# Image hashing.

Image.MAX_IMAGE_PIXELS = None

# MD5 hash computation.
def md5_hasher(image):
  return hashlib.md5(image).hexdigest(), 1, len(image)

# Average hash computation.
# See:
# https://www.hackerfactor.com/blog/index.php?/archives/432-Looks-Like-It.html

def average2x32_hasher(image):
  try:
    # Read image.
    img = Image.open(io.BytesIO(image))

    # Simplify image by reducing its size and colors.
    pixels = img.convert("L").resize((8, 8), Image.LANCZOS).getdata()

    # Get the average pixel value.
    sum = 0.0
    for p in pixels: sum += p
    mean = sum / 64

    # Generate the hash by comparing each pixel value to the average.
    bitshi = 0
    for i in range(32):
      bitshi <<= 1
      if pixels[i] > mean: bitshi |= 1
    bitslo = 0
    for i in range(32, 64):
      bitslo <<= 1
      if pixels[i] > mean: bitslo |= 1
    fingerprint = "%08x%08x" % (bitshi, bitslo)

    return fingerprint, img.width, img.height
  except Exception:
    # Fall back to MD hashing if fingerprint cannot be computed.
    return md5_hasher(image)

def average_hasher(image):
  try:
    # Read image.
    img = Image.open(io.BytesIO(image))

    # Simplify image by reducing its size and colors.
    pixels = img.convert("L").resize((8, 8), Image.LANCZOS).getdata()

    # Get the average pixel value.
    sum = 0.0
    for p in pixels: sum += p
    mean = sum / 64

    # Generate the hash by comparing each pixel value to the average.
    bits = 0
    for p in pixels:
      bits <<= 1
      if p > mean: bits |= 1
    fingerprint = "%016x" % bits

    return fingerprint, img.width, img.height
  except Exception:
    # Fall back to MD hashing if fingerprint cannot be computed.
    return md5_hasher(image)

image_hashers = {
  "md5": md5_hasher,
  "average": average_hasher,
  "average2x32": average2x32_hasher,
}

class Photo:
  def __init__(self, item, url):
    self.item = item
    self.url = url
    self.fingerprint = None
    self.width = None
    self.height = None
    self.nsfw = None

  def size(self):
    return self.width * self.height

photo_cache = {}

def load_photo_cache(filename):
  if filename is None: return
  print("Reading cached fingerprints from", filename)
  fprecs = sling.RecordReader(flags.arg.fpcache)
  hashalg = flags.arg.hash
  for url, fpdata in fprecs:
    url = url.decode()
    fpinfo = json.loads(fpdata)
    photo = Photo(None, url)
    photo.width = fpinfo["width"]
    photo.height = fpinfo["height"]
    photo.fingerprint = fpinfo[hashalg]
    photo_cache[url] = photo
  print(len(photo_cache), "cached fingerprints")
  fprecs.close()

def retrieve_image(url):
  headers = {"User-agent": "SLING Bot 1.0"}
  r = pool.request("GET", url, headers=headers, timeout=60)
  for h in r.retries.history:
    if h.redirect_location.endswith("/removed.png"): return None
    if h.redirect_location.endswith("/no_image.jpg"): return None
  return r

def fetch_image(url):
  # Try to get image from media database.
  global mediadb
  if mediadb is None and flags.arg.mediadb:
    mediadb = sling.Database(flags.arg.mediadb)
  if mediadb:
    image = mediadb[url]
    if image: return image

  # Fetch image from source if it is not in the media database.
  try:
    print("fetch", url)
    r = retrieve_image(url)
    if r is None:
      print("missing", url)
      return None

    if r.status != 200:
      print("error", r.status, url)
      return None

    return r.data
  except Exception as e:
    print("fail", e, url)
    return None

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

  for url, data in fingerprintdb()[missing].items():
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
    self.captions = flags.arg.captions
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
    if result == sling.DBUNCHANGED:
      print(self.itemid, "unchanged")
      return False
    else:
      return True

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
    if type(media) is sling.Frame: media = media[n_is]
    if media.startswith('!'): media = media[1:]
    return media

  # Return nsfw status for media.
  def isnsfw(self, media):
    if type(media) is sling.Frame:
      if media[n_is].startswith('!'): return True
      if media[n_has_quality] == n_nsfw: return True
      return False
    else:
      return media.startswith('!')

  # Return caption for photo.
  def caption(self, media):
    if type(media) is sling.Frame and n_legend in media:
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
      url = self.url(media)
      if is_video(url): continue
      urls.append(url)
    load_fingerprints(urls)

  # Import all photos from another profile.
  def copy(self, other):
    num_added = 0
    for media in other.media():
      url = self.url(media)
      nsfw = self.isnsfw(media)
      caption = self.caption(media)
      num_added += self.add_photo(url, caption, None, nsfw)
    return num_added

  # Remove all matching photos from another profile.
  def remove(self, other):
    # Get fingerprints for photos in profile.
    photos = {}
    self.preload_fingerprints()
    for media in self.media():
      url = other.url(media)
      photo = get_photo(self.itemid, url)
      if photo is None: continue
      photos[photo.fingerprint] = photo

    # Find duplicates from other profile.
    duplicates = set()
    other.preload_fingerprints()
    for media in other.media():
      url = other.url(media)

      # Get photo information.
      photo = get_photo(other.itemid, url)
      if photo is None: continue

      # Check for duplicate.
      dup = photos.get(photo.fingerprint)
      if dup:
        print("duplicate", dup.url, "of", url)
        duplicates.add(dup.url)

    # Remove duplicates.
    if len(duplicates) > 0:
      # Find photos to keep.
      keep = []
      for media in self.media():
        url = self.url(media)
        if url not in duplicates: keep.append(media)
      self.replace(keep)

    return len(duplicates)

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
      urls.append(self.url(media))
    return urls

  # Return photo info for all media.
  def photos(self):
    photos = {}
    for media in self.media():
      url = self.url(media)
      photo = get_photo(self.itemid, url)
      photo.nsfw = self.isnsfw(media)
      photos[url] = photo
    return photos

  # Check if photo already in profile.
  def has(self, url, alturl=None):
    for media in self.media():
      u = self.url(media)
      if u == url or u == alturl: return True
    return False

  # Add photo to profile.
  def add_photo(self, url, caption=None, source=None, nsfw=None):
    # Check if photo should be excluded.
    if self.excluded and url in self.excluded:
      print("Skip excluded photo:", url)
      return 0

    # Check if photo exists.
    if flags.arg.check:
      r = pool.request("HEAD", url)
      if r.status // 100 == 3:
        redirect = r.headers['Location']
        if redirect.endswith("/removed.png"):
          print("Skip removed photo:", url, r.status)
          return 0

        # Check if redirect exists.
        r = pool.request("HEAD", redirect)
        if r.status != 200:
          print("Skip missing redirect:", url, r.status)
          return 0

        # Use redirected url.
        url = redirect
      elif r.status != 200:
        print("Skip missing photo:", url, r.status)
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

    # Add media to profile.
    if nsfw: url = "!" + url
    slots = [(n_is, url)]
    if flags.arg.fixedcaption: caption = flags.arg.fixedcaption
    if caption and self.captions: slots.append((n_legend, caption))
    if source: slots.append((n_stated_in, store[source]))
    if len(slots) == 1:
      self.frame.append(n_media, url)
    else:
      self.frame.append(n_media, store.frame(slots))

    return 1

  # Add Imgur album.
  def add_imgur_album(self, albumid, caption, nsfw_override=None):
    print("Imgur album", albumid)
    auth = {'Authorization': "Client-ID " + imgurkeys["clientid"]}
    url = "https://api.imgur.com/3/album/" + albumid
    r = pool.request("GET", url, headers=auth)
    if r.status == 404:
      print("Skipping missing album", albumid)
      return 0
    if r.status == 403:
      print("Skipping unaccessible album", albumid)
      return 0
    if r.status != 200:
      print("HTTP error", r.status, url)
      return 0

    reply = json.loads(r.data)["data"]
    #print(json.dumps(reply, indent=2))

    serial = 1
    images = reply.get("images")
    if images is None: return 0
    album_title = reply.get("title")
    if album_title is None:
       album_title = caption
    elif caption is not None and caption.startswith(album_title):
      album_title = caption

    count = 0
    for image in images:
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
  def add_imgur_image(self, imageid, caption=None, nsfw_override=None):
    print("Imgur image", imageid)
    auth = {'Authorization': "Client-ID " + imgurkeys["clientid"]}
    url = "https://api.imgur.com/3/image/" + imageid
    r = pool.request("GET", url, headers=auth)
    if r.status == 404:
      print("Skipping missing image", imageid)
      return 0
    if r.status != 200:
      print("HTTP error", r.status, url)
      return 0

    reply = json.loads(r.data)["data"]
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
    if caption is None:
      caption = reply["title"]
    if caption is None:
      caption = reply["description"]
    if caption is not None:
      caption = caption.replace("\n", " ").strip()

    # NSFW flag.
    nsfw = tri(reply["nsfw"] or reply["nsfw"], nsfw_override)

    # Add media to profile frame.
    return self.add_photo(link, caption, None, nsfw)

  # Add Reddit gallery.
  def add_reddit_gallery(self, galleryid, caption, nsfw_override=None):
    print("Redit posting", galleryid)
    while True:
      url = "https://www.reddit.com/comments/%s.json" % galleryid
      r = pool.request("GET", url, headers = {"User-agent": "SLING Bot 1.0"})
      if r.status != 429: break
      reset = int(r.headers.get("x-ratelimit-reset", 60))
      print("gallery rate limit", reset, "secs")
      time.sleep(reset)

    if r.status == 403: return 0
    if r.status != 200:
      print("HTTP error", r.status, url)
      return 0

    children = json.loads(r.data)[0]["data"]["children"]
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
      if not self.captions: title = None
      nsfw = tri(reply["over_18"], nsfw_override)

      count = 0
      if flags.arg.albums:
        # Fetch albums from text.
        selftext = reply["selftext"]
        for m in re.finditer(r"\[(.+)\]\((https?:\/\/imgur.com/a/\w+)\)", selftext):
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

      m = re.match(r"https:\/\/preview\.redd\.it\/(\w+\.\w+)\?", link)
      if m != None: link = "https://i.redd.it/" + m.group(1)

      # Image caption.
      title = reply["title"]
      if title is None:
        title = caption
      elif caption is not None and caption.startswith(title):
        title = caption
      if not self.captions: title = None

      if title != None and flags.arg.numbering:
        title = "%s (%d/%d)" % (title, serial, len(items))

      # NSFW flag.
      nsfw = tri(reply["over_18"], nsfw_override)

      # Add media to profile frame.
      if self.add_photo(link, title, None, nsfw): count += 1
      serial += 1
    return count

  # Add imgchest album.
  def add_imgchest_album(self, albumid, caption, nsfw_override=None):
    print("Image chest album", albumid)
    auth = {"Authorization": "Bearer " + imgchestkeys["token"]}
    url = "https://api.imgchest.com/v1/post/" + albumid
    r = pool.request("GET", url, headers=auth)
    if r.status == 404:
      print("Skipping missing album", albumid)
      return 0
    if r.status == 403:
      print("Skipping unaccessible album", albumid)
      return 0
    if r.status != 200:
      print("HTTP error fetching album", albumid, r.status)
      return 0

    reply = json.loads(r.data)["data"]
    #print(json.dumps(reply, indent=2))

    total = len(reply["images"])
    album_title = reply.get("title")
    if album_title is None:
       album_title = caption
    elif caption is not None and caption.startswith(album_title):
      album_title = caption

    count = 0
    serial = 1
    for image in reply["images"]:
      link = image["link"]

      # Remove query parameters.
      qs = link.find("?")
      if qs != -1: link = link[:qs]

      # Skip anmated GIFs.
      if (not flags.arg.video and image.get("animated", False)):
        print("Skipping animated image", link);
        continue

      # Image caption.
      if flags.arg.perimagecaption:
        title = image.get("title")
        if title is None:
          title = image.get("description")
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
      nsfw = tri(reply["nsfw"], nsfw_override)

      # Add media frame to profile.
      if self.add_photo(link, title, None, nsfw): count += 1
      serial += 1
    return count

  # Add url gallery.
  def add_url_gallery(self, gallery, caption, nsfw_override=None):
    count = 0
    for url in gallery[8:].split(" "):
      nsfw = False
      if url.startswith("!"):
        nsfw = True
        url = url[1:]
      nsfw = tri(nsfw, nsfw_override)

      if self.add_photo(url, caption, None, nsfw): count += 1

    return count

  # Add media.
  def add_media(self, url, caption=None, nsfw=None):
    # NSFW urls.
    if url.startswith("!"):
      nsfw = True
      url = url[1:]

    # Trim url.
    url = url.replace("/i.imgur.com/", "/imgur.com/")
    url = url.replace("/www.imgur.com/", "/imgur.com/")
    url = url.replace("/m.imgur.com/", "/imgur.com/")

    m = re.match(r"(https://imgur\.com/.+)\.jpeg", url)
    if m != None: url = m.group(1) + ".jpg"

    url = url.replace("/www.reddit.com/", "/reddit.com/")
    url = url.replace("/old.reddit.com/", "/reddit.com/")

    if url.startswith("http://reddit.com"): url = "https" + url[4:]
    if url.startswith("http://imgur.com"): url = "https" + url[4:]

    m = re.match(r"https:\/\/reddit\.com\/media\?url=(.+)", url)
    if m != None: url = urllib.parse.unquote(m.group(1))

    m = re.match(r"(https:\/\/imgur\.com/.+)[\?#].*", url)
    if m == None: m = re.match(r"(https?://reddit\.com/.+)[\?#].*", url)
    if m == None: m = re.match(r"(https?://i\.redd\.it/.+)[\?#].*", url)
    if m == None: m = re.match(r"(https?://i\.redditmedia\.com/.+)[\?#].*", url)
    if m != None:
      url = m.group(1)
      if url.endswith("/new"): url = url[:-4]

    m = re.match(r"https://preview.redd.it/(\w+.png)\?.+", url)
    if m != None: url = "https://i.redd.it/" + m.group(1)

    m = re.match(r"(https://imgur\.com/.+\.jpe?g)-\w+", url)
    if m != None: url = m.group(1)

    # Discard videos.
    if not flags.arg.video and is_video(url):
      print("Skipping video", url)
      return 0

    # Discard subreddits.
    m = re.match(r"^https?://reddit\.com/r/\w+/?$", url)
    if m != None: return 0;

    # Discard empty urls.
    if len(url) == 0: return 0

    # Imgur album.
    m = re.match(r"https://imgur\.com/a/(\w+)", url)
    if m != None:
      albumid = m.group(1)
      return self.add_imgur_album(albumid, caption, nsfw)

    # Imgur gallery.
    m = re.match(r"https://imgur\.com/gallery/(\w+)", url)
    if m != None:
      galleryid = m.group(1)
      return  self.add_imgur_album(galleryid, caption, nsfw)
    m = re.match(r"https?://imgur\.com/\w/\w+/(\w+)", url)
    if m != None:
      galleryid = m.group(1)
      return  self.add_imgur_album(galleryid, caption, nsfw)

    # Single-image imgur.
    m = re.match(r"https://imgur\.com/(\w+)$", url)
    if m != None:
      imageid = m.group(1)
      return self.add_imgur_image(imageid, caption, nsfw)

    # Reddit gallery.
    m = re.match(r"https://reddit\.com/gallery/(\w+)", url)
    if m != None:
      galleryid = m.group(1)
      return self.add_reddit_gallery(galleryid, caption, nsfw)

    # Reddit posting.
    m = re.match(r"https://(www\.)?reddit\.com/\w+/.+/comments/(\w+)/", url)
    if m != None:
      galleryid = m.group(2)
      return self.add_reddit_gallery(galleryid, caption, nsfw)

    # Reddit preview.
    m = re.match(r"https://preview.redd.it/(\w+.png)\?", url)
    if m != None:
      imagename = m.group(1)
      url = "https://i.redd.it/" + imagename
    m = re.match(r"https://preview.redd.it/(\w+.jpg)\?", url)
    if m != None:
      imagename = m.group(1)
      url = "https://i.redd.it/" + imagename

    # DR image scaler.
    m = re.match(r"https://asset.dr.dk/[Ii]mage[Ss]caler/\?(.+)", url)
    if m != None:
      q = urllib.parse.parse_qs(m.group(1))
      url = "https://%s/%s" % (q["server"][0], q["file"][0])

    # Image chest album.
    m = re.match(r"https://(?:www\.)?imgchest.com/p/(\w+)", url)
    if m != None:
      albumid = m.group(1)
      return self.add_imgchest_album(albumid, caption, nsfw)

    # Listal thumb.
    m = re.match(r"(https://lthumb.lisimg.com/[0-9\/].jpg)\?.+", url)
    if m != None: url = m.group(1)

    # URL gallery.
    if url.startswith("gallery:"):
      return self.add_url_gallery(url, caption, nsfw)

    # Add media to profile.
    return self.add_photo(url, caption, flags.arg.source, nsfw)

  # Add albums in comments.
  def add_albums_in_comments(self, url, nsfw=None):
    print("Redit albums in commens", url)
    while True:
      r = pool.request("GET", url + ".json",
                       headers = {"User-agent": "SLING Bot 1.0"})
      if r.status != 429: break
      reset = int(r.headers.get("x-ratelimit-reset", 60))
      print("album rate limit", reset, "secs")
      time.sleep(reset)
    if r.status != 200:
      print("HTTP error", r.status)
      return 0

    comments = json.loads(r.data)[1]["data"]["children"]
    count = 0
    for comment in comments:
      comment = comment["data"]
      body = comment["body"]

      for pat in album_patterns:
        for m in pat.finditer(body):
          if len(m.groups()) == 2:
            print("Album link", m[1], m[2])
            count += self.add_media(m[2], m[1], nsfw)
          else:
            print("Album link", m[1])
            count += self.add_media(m[1], None, nsfw)
        if count > 0: break

    return count

  # Remove duplicate photos.
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
      url = self.url(media)
      if is_video(url): continue
      nsfw = self.isnsfw(media)
      captioned = self.captioned(media)
      if captioned: captions[url] = self.caption(media)

      # Get photo information.
      photo = get_photo(self.itemid, url)
      if photo is None:
        missing.add(url)
        continue

      # Check for duplicate.
      dup = photos.get(photo.fingerprint)

      if dup != None:
        if flags.arg.preservecaptioned and captioned:
          print(self.itemid, url, " preserve captioned duplicate of", dup.url)
        else:
          # Keep biggest captioned photo.
          caption = captions.get(url)
          dupcaption = captions.get(dup.url)
          bigger = photo.size() * 0.8 > dup.size()
          if bigger or (caption is not None and dupcaption is None):
            # Remove previous duplicate.
            duplicates.add(dup.url)
            msg = "replacement for"
          else:
            # Remove this duplicate.
            duplicates.add(photo.url)
            msg = "duplicate of"

          if nsfw:
            if dup not in naughty: msg = "nsfw " + msg
          else:
            if dup in naughty: msg = "sfw " + msg

          if caption is not None: msg = "captioned " + msg
          if dupcaption is not None: msg = msg + " captioned"

          if photo.size() < dup.size():
            msg = "smaller " + msg
          elif photo.size() > dup.size():
            msg = "bigger " + msg

          print(self.itemid, url, msg, dup.url)

      # Add photo fingerprint for new or bigger photos.
      if dup is None or dup.size() < photo.size():
        photos[photo.fingerprint] = photo

      if nsfw: naughty.add(photo)
      num_photos += 1

    # Remove duplicates.
    if len(duplicates) > 0 or len(missing) > 0:
      # Find photos to keep.
      keep = []
      for media in self.media():
        url = self.url(media)
        if url not in duplicates and url not in missing: keep.append(media)
      self.replace(keep)

      print(self.itemid,
          num_photos, "photos,",
          len(duplicates), "duplicates",
          len(missing), "missing")

    return len(duplicates) + len(missing)

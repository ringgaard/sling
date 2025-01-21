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

"""Fetch media files and store in media cache database."""

import email.utils
import time
import requests
import sys
import traceback

import sling
import sling.media.photo as photo
import sling.flags as flags

flags.define("--kb",
             default="data/e/kb/kb.sling",
             help="Knowledge base with media references")

flags.define("--max_media_size",
             help="Maximum media file size",
             default=63*1024*1024,
             type=int,
             metavar="SIZE")

flags.define("--blacklist",
             default="local/media-blacklist.txt",
             help="List of blacklisted media files")

flags.define("--whitelist",
             default="local/media-whitelist.txt",
             help="List of already cached media files")

flags.define("--auto_blacklist",
             help="add large images to blacklist",
             default=False,
             action="store_true")

flags.parse()

wiki_base_url = "https://upload.wikimedia.org/wikipedia/"
commons_redirect = "https://commons.wikimedia.org/wiki/Special:Redirect/file/"

# Read list of urls.
def read_urls(filename):
  list = set()
  if filename != None:
    with open(filename, "r") as f:
      for line in f.readlines():
        url = line.strip()
        tab = url.find('\t')
        if tab != -1: url = url[:tab]
        list.add(url);
  return list

# Find media files in knowledge base.
def get_media_files(whitelist):
  # Load knowledge base.
  kb = sling.Store()
  kb.load(flags.arg.kb)

  n_media = kb["/w/media"]
  n_role = kb["role"]
  n_target = kb["target"]
  p_media = kb["media"]

  # Find all properties for WikiCommons files.
  imageprops = set()
  for name, prop in kb["/w/entity"]:
    if name != n_role: continue;
    if prop[n_target] == n_media:
      imageprops.add(prop)

  # Find media files for all items.
  media = []
  mediamap = {}
  for item in kb:
    itemid = item.id;
    for n, v in item:
      if n in imageprops:
        # Add Wikimedia Commons url.
        v = kb.resolve(v)
        if type(v) == str:
          url = photo.commons_media(v)
          media.append(url)
        else:
          print("Bad media file name:", item.id, v)
      elif n == p_media:
        # Add media url.
        url = kb.resolve(v)
        if type(url) == str:
          if url.startswith('!'): url = url[1:]
          if url not in whitelist:
            media.append(url)
            mediamap[url] = itemid
        else:
          print("Bad media url:", item.id, v)

  return media, mediamap

# Read blacklist and whitelist.
blacklist = read_urls(flags.arg.blacklist)
whitelist = read_urls(flags.arg.whitelist)
print(len(blacklist), "blacklisted media files")
print(len(whitelist), "whitelisted media files")
fblack = open(flags.arg.blacklist, "a") if flags.arg.auto_blacklist else None

# Get all media files.
media, mediamap = get_media_files(whitelist)
print(len(media), "media files in knowledge base")

# Connect to media database.
photo.mediadb = sling.Database(flags.arg.mediadb, "mediaload")

# Fetch all missing media files.
num_urls = 0
num_blacklist = 0
num_whitelist = 0
num_known = 0
num_retrieved = 0
num_errors = 0
num_missing = 0
num_toobig = 0
num_bytes = 0
for url in media:
  # Discard blacklisted images.
  num_urls += 1
  if url in blacklist:
    num_blacklist += 1
    continue
  if url in whitelist:
    num_whitelist += 1
    continue

  # Check if url is already in media database.
  if url in photo.mediadb:
    num_known += 1
    continue

  # Download image.
  try:
    r = photo.retrieve_image(url)
    if r == None:
      print("removed", url, mediamap.get(url))
      num_missing += 1
      continue
    elif r.status == 404 and url.startswith(wiki_base_url):
      # Try to get image through the Special:Redirect service.
      slash = url.rfind('/')
      if slash != -1:
        redir = commons_redirect + url[slash + 1:]
        r = photo.retrieve_image(redir)
        if r == None: continue
        if r.status == 200:
          print("redirect", url, "->", r.url)
        else:
          continue
    elif r.status != 200:
      num_errors += 1
      print("error", r.status, url, mediamap.get(url))
      continue
  except Exception as e:
    print("fail", e, url, mediamap.get(url))
    num_errors += 1
    continue

  # Get modification timestamp.
  date = None
  if "Last-Modified" in r.headers:
    date = r.headers["Last-Modified"]
  elif "Date" in r.headers:
    date = r.headers["Date"]
  if date:
    ts = email.utils.parsedate_tz(date)
    last_modified = int(email.utils.mktime_tz(ts))
  else:
    last_modified = int(time.time())

  # Check if image is too big.
  image = r.data
  if len(image) > flags.arg.max_media_size:
    print("too big", len(image), url)
    num_toobig += 1
    if fblack: fblack.write(url + "\n")
    continue

  # Check if image is empty.
  if len(image) == 0:
    print("empty", url)
    continue

  # Check content length.
  if "Content-Length" in r.headers:
    length = int(r.headers["Content-Length"])
    if length != len(image):
      print("length mismatch", length, "vs", len(image), url)

  # Check if image is HTML-like.
  if image.startswith(b"<!doctype html>") or \
     image.startswith(b"<!DOCTYPE html>"):
    print("non-image", url)
    continue

  # Save image in media database.
  photo.mediadb.put(url, image, version=last_modified, mode=sling.DBNEWER)

  num_retrieved += 1
  num_bytes += len(image)
  print(num_retrieved, "/", num_urls, url)
  sys.stdout.flush()

if fblack: fblack.close()

print(num_known, "known,",
      num_retrieved, "retrieved,",
      num_errors, "errors,",
      num_missing, "missing",
      num_toobig, "too big",
      num_blacklist, "blacklisted",
      num_whitelist, "whitelisted",
      num_bytes, "bytes")

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

import requests
import hashlib
import urllib
import sys
import time
import traceback

import sling
import sling.flags as flags

flags.define("--kb",
             default="data/e/wiki/kb.sling",
             help="Knowledge base with media references")

flags.define("--mediadb",
             default="http://localhost:7070/media",
             help="Media database")

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

flags.parse()

commons_base_url = "https://upload.wikimedia.org/wikipedia/commons"
commons_redirect = "https://commons.wikimedia.org/wiki/Special:Redirect/file/"
user_agent = "SLING/1.0 bot (https://github.com/ringgaard/sling)"
session = requests.Session()

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

# Compute MD5 hash for string.
def md5hash(s):
  md5 = hashlib.md5()
  md5.update(s.encode("utf8"))
  return md5.hexdigest()

# Find media files in knowledge base.
def get_media_files():
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
  for item in kb:
    for n, v in item:
      if n in imageprops:
        # Add Wikimedia Commons url.
        v = kb.resolve(v)
        if type(v) == str:
          fn = v.replace(' ', '_')
          md5 = md5hash(fn)
          fn = fn.replace("?", "%3F")
          fn = fn.replace("+", "%2B")
          fn = fn.replace("&", "%26")
          url = "%s/%s/%s/%s" % (commons_base_url, md5[0], md5[0:2], fn)
          media.append(url)
        else:
          print("Bad media file name:", item.id, v)
      elif n == p_media:
        # Add media url.
        v = kb.resolve(v)
        if type(v) == str:
          media.append(v)
        else:
          print("Bad media url:", item.id, v)

  return media

# Get all media files.
media = get_media_files()
print(len(media), "media files in knowledge base")

# Read blacklist and whitelist.
blacklist = read_urls(flags.arg.blacklist)
whitelist = read_urls(flags.arg.whitelist)
print(len(blacklist), "blacklisted media files")
print(len(whitelist), "whitelisted media files")

# Fetch all missing media files.
num_urls = 0
num_blacklist = 0
num_whitelist = 0
num_known = 0
num_retrieved = 0
num_errors = 0
num_toobig = 0
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
  dburl = flags.arg.mediadb + "/" + urllib.parse.quote(url)
  r = session.head(dburl)
  if r.status_code == 200 or r.status_code == 204:
    num_known += 1
    continue
  if r.status_code != 404: r.raise_for_status()

  # Download image.
  try:
    r = session.get(url, headers={"User-Agent": user_agent}, timeout=60)
    if r.status_code == 404 and url.startswith(commons_base_url):
      # Try to get image through the Special:Redirect service.
      slash = url.rfind('/')
      if slash != -1:
        redir = commons_redirect + url[slash + 1:]
        r = session.get(redir, headers={"User-Agent": user_agent}, timeout=60)
        if r.ok: print("redirect", url, "->", r.url)
    if not r.ok:
      num_errors += 1
      print("error", r.status_code, url)
      continue
  except Exception as e:
    print("fail", e, url)
    num_errors += 1
    continue

  last_modified = r.headers["Last-Modified"]
  image = r.content
  if len(image) > flags.arg.max_media_size:
    print("too big", len(image), url)
    num_toobig += 1
    continue

  # Save image in media database.
  r = session.put(dburl, data=image, headers={
    "Last-Modified": last_modified,
    "Mode": "newer",
  })
  r.raise_for_status()

  num_retrieved += 1
  print(num_retrieved, "/", num_urls, r.headers["Result"], url)
  sys.stdout.flush()

print(num_known, "known,",
      num_retrieved, "retrieved,",
      num_errors, "errors,",
      num_toobig, "too big",
      num_blacklist, "blacklisted",
      num_whitelist, "whitelisted")


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

"""SLING case image caching"""

import email.utils
import threading
import requests
import sling
import sling.flags as flags
import sling.media.photo

user_agent = "SLING/1.0 bot (https://github.com/ringgaard/sling)"
mediadb = None
session = sling.media.photo.session

def cache_images(media):
  # Connect to media database.
  global mediadb
  if mediadb is None:
    mediadb = sling.Database(flags.arg.mediadb, "case image cache")

  # Find all images in case.
  num_known = 0
  num_missing = 0
  num_errors = 0
  num_retrieved = 0
  num_images = 0
  num_bytes = 0
  for url in media:
    num_images += 1

    # Check if url is already in media database.
    if url in mediadb:
      num_known += 1
      continue

    # Download image.
    try:
      r = session.get(url,
                      headers={"User-Agent": user_agent},
                      allow_redirects=False,
                      timeout=60)
      if r.status_code == 301:
        redirect = r.headers['Location']
        if redirect.endswith("/removed.png"):
          num_missing += 1
          print("removed", url)
          continue

        # Get redirected image.
        r = session.get(redirect,
                        headers={"User-Agent": user_agent},
                        allow_redirects=False,
                        timeout=60)
        if r.status_code != 200:
          print("missing", url, r.status_code)
          num_missing += 1
          continue
      if not r.ok:
        num_errors += 1
        print("error", r.status_code, url)
        continue
      if r.status_code == 302:
        # Imgur returns redirect to removed.png for missing images.
        num_missing += 1
        print("missing", url)
        continue
    except Exception as e:
      print("fail", e, url)
      num_errors += 1
      continue

    # Check if image is empty.
    image = r.content
    if len(image) == 0:
      print("empty", url)
      num_missing += 1
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

    # Check if image is HTML-like.
    if image.startswith(b"<!doctype html>") or \
       image.startswith(b"<!DOCTYPE html>"):
      print("non-image", url)
      num_errors += 1
      continue

    # Save image in media database.
    mediadb.put(url, image, version=last_modified, mode=sling.DBNEWER)

    num_retrieved += 1
    num_bytes += len(image)
    print("cached", url, len(image))

  print("Caching done:",
        num_images, "images,",
        num_retrieved, "retrieved,",
        num_known, "known,",
        num_missing, "missing,",
        num_errors, "errors",
        num_bytes, "bytes")

def start_image_caching(media):
  t = threading.Thread(target=cache_images, args=(media,))
  t.daemon = True
  t.start()


# Copyright 2023 Ringgaard Research ApS
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

"""Re-fetch photo from source and update media database"""

import urllib3
import email.utils
import sling
import sling.flags as flags

flags.define("--url",
             default=None,
             help="photo url")

flags.define("--mediadb",
             help="database for images",
             default="vault/media",
             metavar="DB")

flags.parse()

mediadb = sling.Database(flags.arg.mediadb, "refreshpic")
pool = urllib3.PoolManager()

def fetch(url):
  # Fetch image.
  r = pool.request("GET", url, timeout=60)
  for h in r.retries.history:
    if h.redirect_location.endswith("/removed.png"):
      raise Exception("image removed")
    if h.redirect_location.endswith("/no_image.jpg"):
      raise Exception("no image")
  if r.status != 200:
    raise Exception("error " + str(r.status) + "fetching image")
  image = r.data

  # Check content length.
  if "Content-Length" in r.headers:
    length = int(r.headers["Content-Length"])
    if length != len(image):
      raise Exception("length mismatch %d vs %d" % (length, len(image)))

  return r.data, r.headers["Last-Modified"]

image, modtime = fetch(flags.arg.url)

last_modified = None
if modtime:
  ts = email.utils.parsedate_tz(modtime)
  last_modified = int(email.utils.mktime_tz(ts))

result = mediadb.put(flags.arg.url, image, last_modified)
print(flags.arg.url, len(image), modtime, result)


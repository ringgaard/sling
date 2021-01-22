# Copyright 2020 Ringgaard Research ApS
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

"""Fetch profile information from Twitter"""

import json
import requests
import sys
import time
import tweepy
import urllib
import sling
import sling.flags as flags

flags.define("--apikeys",
             default="local/keys/twitter.json",
             help="Twitter API key file")

flags.define("--twitterdb",
             help="database for storing Twitter profiles",
             default="http://localhost:7070/twitter",
             metavar="DBURL")

flags.define("--mediadb",
             help="database for storing Twitter profiles pictures",
             default=None,
             metavar="DBURL")

flags.define("--update",
             help="refresh all updated profiles",
             default=False,
             action="store_true")

flags.define("--missing_images",
             help="file with images which needs to be refreshed",
             default=None,
             metavar="FILE")

flags.parse()
session = requests.Session()

bad_images = set([
  "http://pbs.twimg.com/profile_images/1302121919014207490/KaYYEC8b.jpg"
])

# Find all twitter users in knowledge base.
def get_twitter_usernames():
  # Load knowledge base.
  kb = sling.Store()
  kb.load("data/e/wiki/kb.sling")
  p_twitter = kb["P2002"]

  # Find twitter usernames for items.
  users = []
  for item in kb:
    for username in item(p_twitter):
      username = kb.resolve(username)
      if username is not None: users.append(username)

  return users

# Load profile image into media database.
def cache_profile_image(profile):
  if "error" in profile: return
  if profile["default_profile_image"]: return

  imageurl = profile["profile_image_url"]
  imageurl = ''.join(imageurl.rsplit("_normal", 1))
  if imageurl in bad_images: return

  r = session.get(imageurl)
  if not r.ok:
    print("error fetching", r.status_code, imageurl)
    return

  mediaurl = flags.arg.mediadb + "/" + urllib.parse.quote(imageurl)
  last_modified = r.headers["Last-Modified"]
  image = r.content
  r = session.put(mediaurl, data=image, headers={
    "Last-Modified": last_modified,
    "Mode": "newer",
  })
  r.raise_for_status()

# Connect to Twitter.
with open(flags.arg.apikeys, "r") as f:
  apikeys = json.load(f)

auth = tweepy.OAuthHandler(apikeys["consumer_key"], apikeys["consumer_secret"])
auth.set_access_token(apikeys["access_key"], apikeys["access_secret"])
api = tweepy.API(auth, wait_on_rate_limit=True, wait_on_rate_limit_notify=True)

# Collect twitter usernames.
users = get_twitter_usernames()
print(len(users), "twitter usernames")

# Read list of missing profile images.
missing_images = set()
if flags.arg.missing_images:
  with open(flags.arg.missing_images, "r") as f:
    for line in f.readlines():
      url = line.strip()
      missing_images.add(url)
  print(len(missing_images), "missing images")

# Refresh twitter profiles.
num_users = 0
num_fetched = 0
num_updated = 0
for username in users:
  # Check if twitter user is already known.
  num_users += 1
  dburl = flags.arg.twitterdb + "/" + urllib.parse.quote(username)
  current = None
  if flags.arg.update:
    # Get current profile.
    r = session.get(dburl)
    if r.status_code == 200 or r.status_code == 204:
      # Known profile.
      current = r.json()
      if "error" in current: continue
      if len(missing_images) > 0:
        imageurl = current["profile_image_url"]
        imageurl = ''.join(imageurl.rsplit("_normal", 1))
        if imageurl not in missing_images: continue
  else:
    # Skip if known.
    r = session.head(dburl)
    if r.status_code == 200 or r.status_code == 204: continue

  if r.status_code != 404: r.raise_for_status()

  # Fetch twitter profile for item.
  profile = None
  try:
    user = api.get_user(username)
    profile = user._json
  except tweepy.TweepError as e:
    print("Error fetching twitter profile", username, ":", e)
    profile = {"error": e.api_code, "message": e.reason}
    if current: continue

  # Check if profile has been updated.
  if flags.arg.update:
    updated = False
    if profile["profile_image_url"] != current["profile_image_url"]:
      updated = True
    if updated:
      print("Update", username)
    else:
      continue

  # Write profile information to database.
  r = session.put(dburl, json=profile, headers={
    "Version": str(int(time.time())),
    "Mode": "overwrite" if flags.arg.update else "add"
  })
  r.raise_for_status()

  # Optionally store profile image in media db.
  if flags.arg.mediadb: cache_profile_image(profile)

  num_fetched += 1
  print(num_fetched, num_users, username)
  sys.stdout.flush()

print("Done", num_fetched, "/", num_users, "twitter users updated")


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

"""Find photos of persons in reddit postings."""

import html
import json
import requests
import datetime
import re
import sys
import traceback
from urllib.parse import urlparse
from collections import defaultdict

import sling
import sling.flags as flags
import sling.media.photo as photo
import sling.util

flags.define("--redditdb",
             help="database for reddit postings",
             default="reddit",
             metavar="DB")

flags.define("--photoids",
             help="table with photo fingerprints",
             default="data/e/media/photoid.rec",
             metavar="FILE")

flags.define("--posting",
             help="single reddit posting",
             default=None,
             metavar="SID")

flags.define("--postings",
             help="input record file with postings",
             default=None,
             metavar="FILE")

flags.define("--checkpoint",
             help="file with latest checkpoint for scanning reddit db",
             default=None,
             metavar="FILE")

flags.define("--subreddits",
             help="files with subreddits and item ids for finding photos",
             default=None,
             metavar="FILES")

flags.define("--celebmap",
             help="list of names mapped to item ids",
             default=None,
             metavar="FILES")

flags.define("--patterns",
             help="patterns for matching posting titles",
             default=None,
             metavar="FILES")

flags.define("--aliases",
             help="phrase table for matching item names",
             default="data/e/kb/en/phrase-table.repo",
             metavar="FILE")

flags.define("--report",
             help="JSON report for unmatched postings",
             default=None,
             metavar="FILE")

flags.define("--caseless",
             help="case-insesitive matching",
             default=False,
             action="store_true")

flags.define("--dryrun",
             help="do not update database",
             default=False,
             action="store_true")

flags.parse()

# List of approved photo sites.
photosites = set([
  "imgur.com",
  "i.imgur.com",
  "m.imgur.com",
  "www.imgur.com",
  "i.redd.it",
  "old.reddit.com",
  "www.reddit.com",
  "i.redditmedia.com",
  "i.reddituploads.com",
  "media.gettyimages.com",
  "i.pinimg.com",
  "pbs.twimg.com",
  "iv1.lisimg.com",
])

# Name delimiters.
delimiters = [
  "(", "[", ",", " - ", "|", "/", ":", "!", " – ", ";", "'s ", "’s ",
  "...", " -- ", "~", "- ", " -", "<",
  "❤️", "♥️", "❤️", "☺", "✨", "❣", "<3", "•", "❤", "📸",
  " circa ", " c.",
  "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
  " by ", " is ", " was ", " in ", " In", " on ", " with ", " at ", " as ",
  " from ", " for ", " has ",
  " Is ", " Has ", " On ", " As ",
  " aka ", " a.k.a. ", " IG ", " on/off", " On/Off", " Circa "
  " having ", " performing ", " during ", " being ",
  " posing ", " photographed ", " dressed ", " former ", " formerly ",
  " Collection ",

  # For jounalists.
  "FoxWeather", "News", "Fox13", "Weather Channel", "Court TV", "Fox News",
]

# Initialize commons store.
commons = sling.Store()
n_subreddit = commons["subreddit"]
n_title = commons["title"]
n_url = commons["url"]
n_permalink = commons["permalink"]
n_is_self = commons["is_self"]
n_over_18 = commons["over_18"]
n_thumbnail = commons["thumbnail"]
n_preview = commons["preview"]
n_images = commons["images"]
n_resolutions = commons["resolutions"]
n_crosspost = commons["crosspost_parent_list"]
aliases = sling.PhraseTable(commons, flags.arg.aliases)
commons.freeze()

# Read subreddit list.
person_subreddits = {}
general_subreddits = set()
skipped_subreddits = set()
aic_subreddits = set()

for fn in flags.arg.subreddits.split(","):
  with open(fn, "r") as f:
    for line in f.readlines():
      f = line.strip().split(' ')
      sr = f[0]
      itemid = f[1] if len(f) > 1 else None
      if sr in skipped_subreddits: continue
      if itemid == "skip":
        skipped_subreddits.add(sr)
      elif itemid == "aic":
        general_subreddits.add(sr)
        aic_subreddits.add(sr)
      elif itemid is None:
        general_subreddits.add(sr)
      else:
        person_subreddits[sr] = itemid

for sr in skipped_subreddits:
  if sr in person_subreddits: del person_subreddits[sr]
  if sr in general_subreddits: del general_subreddits[sr]

# Read regex patterns for matching posting titles.
patterns = []
for fn in flags.arg.patterns.split(","):
  with open(fn, "r") as f:
    for line in f.readlines():
      line = line.strip()
      if len(line) == 0 or line.startswith("#"): continue
      r = re.compile(line)
      patterns.append(r)

# Read list of celebrity names.
celebmap = {}
for fn in flags.arg.celebmap.split(","):
  with open(fn, "r") as f:
    for line in f.readlines():
      line = line.strip()
      if len(line) == 0: continue
      f = line.split(':')
      if len(f) != 2:
        print("bad line in celebmap:", line)
        continue
      name = f[0].replace(".", "").strip()
      if flags.arg.caseless: name = name.lower()
      itemid = f[1].strip()
      celebmap[name] = itemid

# Photo id table.
photoids = sling.RecordDatabase(flags.arg.photoids)

def get_photo_id(fingerprint):
  info = photoids[fingerprint]
  if info is None: return None
  return json.loads(info)

# Fetch posting from Reddit.
session = requests.Session()
def fetch_posting(store, sid):
  r = session.get("https://api.reddit.com/api/info/?id=" + sid,
                   headers = {"User-agent": "SLING Bot 1.0"})
  if r.status_code != 200: return None
  reply = store.parse(r.content, json=True)
  children = reply["data"]["children"]
  if len(children) == 0: return None
  return children[0]["data"]

# Check for selfies.
def selfie(title):
  for prefix in ["Me ", "My ", "Me,", "Me. ", "my "]:
    if title.startswith(prefix): return True
  return False

# Look up name in name table.
def lookup_name(name):
  if flags.arg.caseless:
    return celebmap.get(name.lower())
  else:
    return celebmap.get(name)

# Get proper name prefix.
def name_prefix(name):
  prefix = []
  for word in name.split(" "):
    if len(word) >= 3 and word.isupper(): break
    if len(word) > 0 and word[0].isupper():
      prefix.append(word)
    else:
      break
  if len(prefix) < 2: return None
  return " ".join(prefix).strip(" .,-?")

# Get size of photo in pixels.
def pixels(info):
  return info["width"] * info["height"]

# Find new postings to subreddits.
redditdb = sling.Database(flags.arg.redditdb, "redphotos")
chkpt = sling.util.Checkpoint(flags.arg.checkpoint)
report = {}
report["subreddits"] = {}
seen = set()
fingerprints = {}
profiles = {}

num_profiles = 0
num_photos = 0
num_dups = 0
num_known = 0
num_unknown = 0
num_reposts = 0
num_removed = 0
num_selfies = 0
num_errors = 0

if flags.arg.posting:
  postings = [(flags.arg.posting, redditdb[flags.arg.posting])]
elif flags.arg.postings:
  postings = sling.RecordReader(flags.arg.postings)
else:
  postings = redditdb.items(chkpt.checkpoint)

for key, value in postings:
  # Parse reddit posting.
  store = sling.Store(commons)
  posting = store.parse(value, json=True)
  title = posting[n_title]
  if type(key) is bytes: key = key.decode()

  # Discard self-posts.
  if posting[n_is_self]: continue

  # Check for approved site.
  url = posting[n_url]
  if type(url) != str: continue
  if url is None or len(url) == 0: continue
  if "?" in url: continue
  domain = urlparse(url).netloc
  if domain not in photosites: continue

  # Discard videos.
  if photo.is_video(url): continue

  # Discard duplicate postings.
  if url in seen:
    print(sr, key, "REPOST", title)
    num_reposts += 1
    continue
  seen.add(url)

  # Refetch posting from Reddit to check if it has been deleted.
  posting = fetch_posting(store, key)
  if posting is None or \
     posting["removed_by_category"] or \
     posting["title"] == "[deleted by user]":
    print(sr, key, "REMOVED", title)
    num_removed += 1
    continue

  # Discard posting with bad titles.
  if type(title) is bytes:
    print(sr, key, "BAD", title)
    num_errors += 1
    continue
  title = html.unescape(title.replace('\n', ' ').replace("’", "'").strip())
  nsfw = True if posting[n_over_18] else None

  # Check for personal subreddit.
  sr = posting[n_subreddit]
  itemid = person_subreddits.get(sr)
  general = sr in general_subreddits
  if itemid is None and not general: continue

  # Try to match patterns in title.
  if general:
    for pattern in patterns:
      m = pattern.fullmatch(title)
      if m != None:
        title = m.group(1).strip()

  # Check for name match in general subreddit.
  query = title
  if general:
    # Skip photos with multiple persons.
    if " and " in title: continue
    if " And " in title: continue
    if " & " in title: continue
    if " &amp; " in title: continue
    if " or " in title: continue
    if " vs. " in title: continue
    if " vs " in title: continue
    if " og " in title: continue
    if " gegen " in title: continue

    # Try to match title to name.
    name = title
    cut = len(name)
    for d in delimiters:
      p = name.find(d)
      if p != -1 and p < cut: cut = p
    name = name[:cut].strip(" .,-?—")
    itemid = lookup_name(name)
    query = name

    # Try to match name.
    if itemid is None:
      itemid = lookup_name(name)
      query = name

    # Try to match name prefix.
    if itemid is None:
      prefix = name_prefix(name)
      if prefix != None:
        itemid = lookup_name(prefix)
        query = prefix

  # Add posting to report.
  subreddit = report["subreddits"].get(sr)
  if subreddit is None:
    subreddit = {}
    subreddit["total"] = 0
    subreddit["matches"] = 0
    subreddit["general"] = general
    subreddit["matched"] = []
    subreddit["unmatched"] = []
    report["subreddits"][sr] = subreddit

  subreddit["total"] += 1
  p = {}
  p["posting"] = json.loads(posting.data(json=True, byref=False))

  # Get photos for posting.
  posting_profile = photo.Profile(None)
  try:
    n = 0
    if sr in aic_subreddits or "(AIC)" in title:
      post_url = "https://www.reddit.com" + posting[n_permalink]
      n += posting_profile.add_albums_in_comments(post_url, nsfw)
    if n == 0:
      n += posting_profile.add_media(url, None, nsfw)
  except:
    print("Error processing", url, "for", itemid)
    traceback.print_exc(file=sys.stdout)
  if n == 0:
    print(sr, key, "EMPTY", title)
    continue
  p["photos"] = n

  # Check for duplicates.
  posting_profile.preload_fingerprints()
  keep = []
  for media in posting_profile.media():
    # Get fingerprint for photo.
    posting_photo = photo.get_photo(itemid, posting_profile.url(media))
    if posting_photo == None: continue
    posting_info = {
      "item": posting_photo.item,
      "url": posting_photo.url,
      "width": posting_photo.width,
      "height": posting_photo.height,
      "sr": sr,
    }

    # Try to locate biggest existing photo with matching fingerprint.
    existing_photo = get_photo_id(posting_photo.fingerprint)

    # Check if there is a matching photo in a previous posting.
    previous_photo = fingerprints.get(posting_photo.fingerprint)

    # Use the photo from previous posting for comparison if there is no
    # exising photo in the fingerprint database.
    if existing_photo is None:
      # No matching photo in fingerprint database; try using one from an
      # earlier posting.
      existing_photo = previous_photo
    elif previous_photo is not None:
      # Use photo from earlier posting if it is bigger.
      if pixels(previous_photo) > pixels(existing_photo):
        existing_photo = previous

    if existing_photo is None:
      # Photo has not been seen before.
      keep.append(media)

      # Add photo to local fingerprint cache.
      fingerprints[posting_photo.fingerprint] = posting_info
    else:
      # Duplicate photo.
      existing_size = pixels(existing_photo)
      if "dup" not in p:
        # Add dup information to posting.
        p["dup"] = {
          "item": existing_photo["item"],
          "url": existing_photo["url"],
          "sr": existing_photo.get("sr"),
          "smaller": existing_size < posting_photo.size(),
          "bigger": existing_size > posting_photo.size(),
        }

      # Keep duplicate photo if it is for another item or it is bigger.
      if itemid is not None and itemid != existing_photo["item"]:
        keep.append(media)
      elif posting_photo.size() > existing_size:
        keep.append(media)

      # Overwrite cache if photo is bigger.
      if previous_photo is not None:
        if posting_photo.size() > pixels(previous_photo):
          fingerprints[posting_photo.fingerprint] = posting_info

  posting_profile.replace(keep)
  dups = n - len(keep)
  p["duplicates"] = dups
  num_dups += dups

  if itemid is None:
    if selfie(title):
      print(sr, key, "SELFIE", title, "NSFW" if nsfw else "", url)
      num_selfies += 1
    else:
      matches = aliases.query(query)
      p["query"] = query
      p["matches"] = len(matches)
      if len(matches) == 1: p["match"] = matches[0].id()
      subreddit["unmatched"].append(p)
      num_unknown += 1
  else:
    subreddit["matches"] += 1
    p["itemid"] = itemid
    if general: p["query"] = query
    subreddit["matched"].append(p)
    num_known += 1

    # Add media to photo db.
    profile = profiles.get(itemid)
    if profile is None:
      profile = photo.Profile(itemid)
      profiles[itemid] = profile
      num_profiles += 1
    num_photos += profile.copy(posting_profile)

  print(sr, key, itemid, title, "NSFW" if nsfw else "", url)

# Output JSON report.
if flags.arg.report:
  reportfn = datetime.datetime.now().strftime(flags.arg.report)
  fout = open(reportfn, "w")
  json.dump(report, fout)
  fout.close()

# Write updated profiles.
if not flags.arg.dryrun:
  photo.store.coalesce()
  for id in profiles:
    profiles[id].write()

coverage = int(num_known / (num_known + num_unknown) * 100)
print(num_photos, "photos,",
      num_dups, "dups,",
      num_profiles, "profiles,",
      num_known, "known,",
      num_unknown, "unknown,",
      num_reposts, "reposts,",
      num_removed, "removed,",
      num_selfies, "selfies,",
      num_errors, "errors,",
      coverage, "% coverage",
)

if not flags.arg.dryrun: chkpt.commit(redditdb.position())
redditdb.close()


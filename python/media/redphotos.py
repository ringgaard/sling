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

flags.define("--posting",
             help="single reddit posting",
             default=None,
             metavar="SID")

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

flags.define("--batch",
             help="output file for photo batch list",
             default=None,
             metavar="FILE")

flags.define("--caseless",
             help="case-insesitive matching",
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
])

# Name delimiters.
delimiters = [
  "(", "[", ",", " - ", "|", "/", ":", "!", " – ", ";", "'s ", "’s ",
  "...", " -- ", "~", "- ", " -",
  "❤️", "♥️", "☺", "✨", "❣", "<3", "•", "❤", "📸",
  " circa ", " c.",
  "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
  " by ", " is ", " was ", " in ", " In", " on ", " with ", " at ", " as ",
  " from ", " for ", " has ",
  " Is ", " Has ", " On ", " As ",
  " aka ", " a.k.a. ", " IG ", " on/off", " On/Off", " Circa "
  " having ", " performing ", " during ", " being ",
  " posing ", " photographed ", " dressed ", " former ", " formerly ",

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
general_subreddits = {}
skipped_subreddits = set()
for fn in flags.arg.subreddits.split(","):
  with open(fn, "r") as f:
    for line in f.readlines():
      f = line.strip().split(' ')
      sr = f[0]
      itemid = f[1] if len(f) > 1 else None
      if sr in skipped_subreddits: continue
      if itemid is None:
        general_subreddits[sr] = itemid
      elif itemid == "skip":
        skipped_subreddits.add(sr)
      else:
        person_subreddits[sr] = itemid

# Read regex patterns for mathing posting titles.
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

# Check if posting has been deleted.
session = requests.Session()
def posting_deleted(sid):
  r = session.get("https://api.reddit.com/api/info/?id=" + sid,
                   headers = {"User-agent": "SLING Bot 1.0"})
  if r.status_code != 200: return True
  children = r.json()["data"]["children"]
  if len(children) == 0: return True
  reply = children[0]["data"]
  return reply["removed_by_category"] != None

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

# Find new postings to subreddits.
batch = None
if flags.arg.batch: batch = open(flags.arg.batch, 'w')
redditdb = sling.Database(flags.arg.redditdb, "redphotos")
chkpt = sling.util.Checkpoint(flags.arg.checkpoint)
report = {}
report["subreddits"] = {}
seen = set()
profiles = {}

num_profiles = 0
num_photos = 0
num_known = 0
num_unknown = 0
num_dups = 0
num_deleted = 0
num_selfies = 0

if flags.arg.posting:
  postings = [(flags.arg.posting, redditdb[flags.arg.posting])]
else:
  postings = redditdb.items(chkpt.checkpoint)

for key, value in postings:
  # Parse reddit posting.
  store = sling.Store(commons)
  posting = store.parse(value, json=True)

  # Discard posting with bad titles.
  title = posting[n_title]
  if type(title) is bytes: continue
  title = title.replace('\n', ' ').strip()
  nsfw = posting[n_over_18]

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
  if url.endswith(".gif"): continue
  if url.endswith(".gifv"): continue
  if url.endswith(".mp4"): continue
  if url.endswith(".webm"): continue

  # Check for personal subreddit.
  sr = posting[n_subreddit]
  itemid = person_subreddits.get(sr)
  general = itemid is None

  # Try to match patterns in title.
  if general:
    for pattern in patterns:
      m = pattern.fullmatch(title)
      if m != None:
        title = m.group(1).strip()

  # Check for name match in general subreddit.
  query = title
  if itemid is None:
    if sr in general_subreddits:
      # Skip photos with multiple persons.
      if " and " in title: continue
      if " And " in title: continue
      if " & " in title: continue
      if " &amp; " in title: continue
      if " vs. " in title: continue
      if " vs " in title: continue

      # Try to match title to name.
      name = title
      cut = len(name)
      for d in delimiters:
        p = name.find(d)
        if p != -1 and p < cut: cut = p
      name = name[:cut].strip(" .,-?—")
      itemid = lookup_name(name)
      query = name

      # Try to match up until first period.
      if itemid is None:
        period = title.find(". ")
        if period != -1:
          name = title[:period]
          itemid = lookup_name(name)
          query = name

      # Try to match name prefix.
      if itemid is None:
        prefix = name_prefix(name)
        if prefix != None:
          itemid = lookup_name(prefix)
          query = prefix
    else:
      continue

  # Check if posting has been deleted.
  if posting_deleted(key):
    print(sr, key, "DELETED", title)
    num_deleted += 1
    continue

  # Discard duplicate postings.
  if url in seen:
    print(sr, key, "DUP", title)
    num_dups += 1
    continue
  seen.add(url)

  # Add posting to report.
  subreddit = report["subreddits"].get(sr)
  if subreddit is None:
    subreddit = {}
    subreddit["total"] = 0
    subreddit["matches"] = 0
    subreddit["matched"] = []
    subreddit["unmatched"] = []
    report["subreddits"][sr] = subreddit

  subreddit["total"] += 1
  p = {}
  p["posting"] = json.loads(value)
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
      print(sr, key, "UNKNOWN", title, "NSFW" if nsfw else "", url)
      num_unknown += 1
  else:
    subreddit["matches"] += 1
    p["itemid"] = itemid
    if general: p["query"] = query
    subreddit["matched"].append(p)
    print(sr, key, itemid, title, "NSFW" if nsfw else "", url)
    num_known += 1

    # Add media to photo db.
    if flags.arg.photodb:
      # Get or create new profile.
      profile = profiles.get(itemid)
      if profile is None:
        profile = photo.Profile(itemid)
        profiles[itemid] = profile
        num_profiles += 1

      try:
        n = profile.add_media(url, None, nsfw)
        num_photos += n
      except:
        print("Error processing", url, "for", itemid)
        traceback.print_exc(file=sys.stdout)

    # Output photo to batch list.
    if batch:
      batch.write("%s %s #\t %s %s%s\n" %
                  (sr, title, itemid, url, " NSFW" if nsfw else ""))

# Output JSON report.
if flags.arg.report:
  reportfn = datetime.datetime.now().strftime(flags.arg.report)
  fout = open(reportfn, "w")
  json.dump(report, fout)
  fout.close()

# Write updated profiles.
photo.store.coalesce()
for id in profiles:
  profiles[id].write()

print(num_photos, "photos,",
      num_profiles, "profiles,",
      num_known, "known,",
      num_unknown, "unknown,",
      num_dups, "dups,",
      num_deleted, "deleted,",
      num_selfies, "selfies")

chkpt.commit(redditdb.position())
redditdb.close()
if batch: batch.close()


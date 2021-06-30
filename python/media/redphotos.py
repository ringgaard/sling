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

import requests
from urllib.parse import urlparse

import sling
import sling.flags as flags
import sling.util

flags.define("--redditdb",
             help="database for reddit postings",
             default="reddit",
             metavar="DB")

flags.define("--checkpoint",
             help="file with latest checkpoint for scanning reddit db",
             default=None,
             metavar="FILE")

flags.define("--subreddits",
             help="list of subreddits and item ids for finding photos",
             default=None,
             metavar="FILE")

flags.define("--celebmap",
             help="list of names mapped to item ids",
             default=None,
             metavar="FILE")

flags.define("--batch",
             help="output file for photo batch list",
             default=None,
             metavar="FILE")

flags.parse()

# Set of black-listed subreddits.
blacklist = set([
  "AdamRagusea",
  "ariheads",
  "Ashens",
  "Behzinga",
  "billsimmons",
  "bingingwithbabish",
  "brandonsanderson",
  "BritneySpears",
  "CaptainSparklez",
  "CDawgVA",
  "codyko",
  "Cr1TiKaL",
  "daverubin",
  "DavidDobrik",
  "davidlynch",
  "Destiny",
  "DoctorMike",
  "douglasadams",
  "DreamWasTaken",
  "earlsweatshirt",
  "elliottsmith",
  "elonmusk",
  "Eminem",
  "EsfandTV",
  "forsen",
  "FrankOcean",
  "HowToBasic",
  "InternetCommentEtiq",
  "jacksepticeye",
  "JacksFilms",
  "JackSucksAtLife",
  "Jaharia",
  "JamesHoffmann",
  "Jazza",
  "JoeBiden",
  "JoeRogan",
  "johnoliver",
  "JordanPeterson",
  "jschlatt",
  "Kanye",
  "KendrickLamar",
  "ksi",
  "lanadelrey", # only PHOTO flair
  "LazarBeam",
  "LeafyIsHere",
  "LGR",
  "liluzivert", # only Image flair
  "lilypichu",
  "Lovecraft",
  "lowspecgamer",
  "LudwigAhgren",
  "MacMiller",
  "Markiplier",
  "Mizkif",
  "mkbhd",
  "MrBeast",
  "murakami",
  "Pete_Buttigieg",
  "PewdiepieSubmissions",
  "PinkOmega",
  "playboicarti",
  "porterrobinson",
  "PostMalone",
  "Ranboo",
  "RTGameCrowd",
  "samharris",
  "Schaffrillas",
  "shakespeare",
  "SomeOrdinaryGmrs",
  "StanleyKubrick",
  "stephenking",
  "terencemckenna",
  "TheWeeknd",
  "tommyinnit",
  "Trainwreckstv",
  "travisscott",
  "tylerthecreator",
  "valkyrae",
  "videogamedunkey",
  "wesanderson",
  "xqcow",
  "XXXTENTACION",
  "YoungThug",
])

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
  "(", "[", ",", " - ", "|", "/", ":", "!", " – ", "'s ", "’s ",
  " circa ", " c. ",
  "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
  " by ", " is ", " in ", " on ", " with ", " at ", " as ", " from ",
  " aka ", " has ", " having ", " performing ", " during ", " being ",
  " posing ", " photographed ", " dressed ",
]

# Initialize commons store.
commons = sling.Store()
n_subreddit = commons["subreddit"]
n_title = commons["title"]
n_url = commons["url"]
n_is_self = commons["is_self"]
n_over_18 = commons["over_18"]
commons.freeze()

# Read subreddit list.
person_subreddits = {}
general_subreddits = {}
for fn in flags.arg.subreddits.split(","):
  with open(fn, "r") as f:
    for line in f.readlines():
      f = line.strip().split(' ')
      sr = f[0]
      itemid = f[1] if len(f) > 1 else None
      if sr in blacklist: continue
      if itemid is None:
        general_subreddits[sr] = itemid
      else:
        person_subreddits[sr] = itemid

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
  for prefix in ["Me ", "My ", "Me,"]:
    if title.startswith(prefix): return True
  return False

# Find new postings to subreddits.
batch = open(flags.arg.batch, 'w')
redditdb = sling.Database(flags.arg.redditdb, "redphotos")
chkpt = sling.util.Checkpoint(flags.arg.checkpoint)
for key, value in redditdb.items(chkpt.checkpoint):
  # Parse reddit posting.
  store = sling.Store(commons)
  posting = store.parse(value, json=True)

  # Discard posting with bad titles.
  title = posting[n_title]
  if type(title) is bytes: continue
  title = title.replace('\n', ' ').strip()

  # Check for personal subreddit.
  sr = posting[n_subreddit]
  itemid = person_subreddits.get(sr)

  # Check for name match in general subreddit.
  if itemid is None:
    if sr in general_subreddits:
      # Skip photos with multiple persons.
      if " and " in title: continue
      if " And " in title: continue
      if " & " in title: continue
      if " &amp; " in title: continue

      # Try to match title to name.
      name = title
      cut = len(name)
      for d in delimiters:
        p = name.find(d)
        if p != -1 and p < cut: cut = p
      name = name[:cut].replace(".", "").strip()

      itemid = celebmap.get(name)
    else:
      continue

  # Discard self-posts.
  if posting[n_is_self]: continue

  # Check for approved site
  url = posting[n_url]
  if url is None or len(url) == 0: continue
  if "?" in url: continue
  domain = urlparse(url).netloc
  if domain not in photosites: continue

  # Discard videos.
  if url.endswith(".gif"): continue
  if url.endswith(".gifv"): continue
  if url.endswith(".mp4"): continue
  if url.endswith(".webm"): continue

  # Check if posting has been deleted.
  if posting_deleted(key): continue

  # Log unknown postings.
  if itemid is None:
    if not selfie(title): print(sr, key, "UNKNOWN", title, url)
    continue

  # Output photo to batch list.
  nsfw = posting[n_over_18]
  batch.write("%s %s #\t %s %s%s\n" %
              (sr, title, itemid, url, " NSFW" if nsfw else ""))

  print(sr, key, itemid, title, "NSFW" if nsfw else "", url)

chkpt.commit(redditdb.position())
redditdb.close()
batch.close()


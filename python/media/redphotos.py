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

flags.parse()

# Set of black-listed subreddits.
blacklist = set([
  "ksi",
  "tommyinnit",
  "PewdiepieSubmissions",
  "Behzinga",
  "billsimmons",
  "CaptainSparklez",
  "Destiny",
  "DreamWasTaken",
  "earlsweatshirt",
  "Eminem",
  "forsen",
  "FrankOcean",
  "Hasan_Piker",
  "jacksepticeye",
  "JackSucksAtLife",
  "Jazza",
  "JoeBiden",
  "JoeRogan",
  "johnoliver",
  "JordanPeterson",
  "Kanye",
  "lanadelrey", # only PHOTO flair
  "LGR",
  "liluzivert", # only Image flair
  "LudwigAhgren",
  "MacMiller",
  "Markiplier",
  "Mizkif",
  "mkbhd",
  "MrBeast",
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

# Initialize commons store.
commons = sling.Store()
n_subreddit = commons["subreddit"]
n_title = commons["title"]
n_url = commons["url"]
n_is_self = commons["is_self"]
n_over_18 = commons["over_18"]
commons.freeze()

# Read subreddit list.
subreddits = {}
with open(flags.arg.subreddits, "r") as f:
  for line in f.readlines():
    f = line.split(' ')
    sr = f[0]
    itemid = f[1]
    if sr in blacklist: continue
    subreddits[sr] = itemid
print("Scan", len(subreddits), "subreddits")

# Find new postings to subreddits.
redditdb = sling.Database(flags.arg.redditdb)
chkpt = sling.util.Checkpoint(flags.arg.checkpoint)
for key, value in redditdb.items(chkpt.checkpoint):
  # Parse reddit posting.
  store = sling.Store(commons)
  posting = store.parse(value, json=True)

  # Discard if subreddit is not in the set of monitored subreddits.
  sr = posting[n_subreddit]
  itemid = subreddits.get(sr)
  if itemid is None: continue

  # Discard posting with bad titles.
  title = posting[n_title]
  if type(title) is bytes: continue

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

  nsfw = posting[n_over_18]

  print(sr, itemid, title, "NSFW" if nsfw else "", url)

#chkpt.commit(redditdb.position())
redditdb.close()


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

"""Scan Reddit for new postings."""

import json
import requests
import sys
import time
import traceback

import sling
import sling.flags as flags
import sling.log as log

flags.define("--redditdb",
             default="reddit",
             help="Reddit submissions database")

flags.define("--subreddits",
             default=None,
             help="File with subreddits that should be archived")

flags.define("--interval",
             help="scan interval",
             default=30*60,
             type=int,
             metavar="SECS")

flags.parse()

redditdb = sling.Database(flags.arg.redditdb)

# Get list of subreddits to monitor.
subreddits = []
for filename in flags.arg.subreddits.split(","):
  with open(filename, "r") as f:
    for line in f.readlines():
      sr = line.strip();
      if len(sr) == 0 or sr[0] == '#': continue;
      pos = sr.find(' ')
      if pos != -1: sr = sr[:pos].strip()
      subreddits.append(sr);
log.info("Monitor", len(subreddits), "subreddits")

def fetch_new_postings():
  # Get new postings from subreddits.
  session = requests.Session()
  for sr in subreddits:
    # Fetch new postings for subreddit.
    r = session.get("https://www.reddit.com/r/" + sr + "/new.json",
                    headers = {"User-agent": "SLING Bot 1.0"})
    if r.status_code == 404:
      log.error("unknown subreddit:", sr)
      continue
    elif r.status_code != 200:
      log.error("http error", r.status_code, "fetching postings:", sr)
      continue

    children = r.json()["data"]["children"]
    for child in children:
      posting = child["data"]
      sid = posting["name"]
      if sid in redditdb: break

      title = posting["title"]
      print("###", sr, sid, title)
      redditdb[sid] = json.dumps(posting)

while True:
  try:
    log.info("Scan for new postings")
    fetch_new_postings()
    log.info("Scan done")
    time.sleep(flags.arg.interval)

  except KeyboardInterrupt as error:
    print("Stopped")
    sys.exit()

  except:
    traceback.print_exc(file=sys.stdout)
    time.sleep(60)


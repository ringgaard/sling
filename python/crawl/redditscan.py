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
import math
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

flags.define("--delay",
             help="delay before checking for removal",
             default=0.5,
             type=float,
             metavar="NUM")

flags.parse()

redditdb = sling.Database(flags.arg.redditdb)
session = requests.Session()

isdst = time.daylight and time.localtime().tm_isdst > 0
tzofs = time.altzone if isdst else time.timezone
midnight = math.ceil((time.time() - tzofs) / 86400) * 86400 + tzofs

queue = []
qsize = 0

class Submission:
  def __init__(self, ts, posting):
    self.ts = ts
    self.posting = posting

  def __lt__(self, other):
    self.ts < other.ts

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

def fetch_posting(sid):
  try:
    while True:
      r = session.get("https://www.reddit.com/comments/%s.json" % sid[3:],
                      headers={"User-agent": "SLING Bot 1.0"}, timeout=180)
      if r.status_code != 429: break
      reset = int(r.headers.get("x-ratelimit-reset", 60))
      log.info("refetch rate limit", reset, "secs")
      time.sleep(reset)

    if r.status_code != 200:
      log.error("fetch error", r.status_code)
      return None
    reply = r.json()
    children = reply[0]["data"]["children"]
    if len(children) == 0: return None
    return children[0]["data"]
  except Exception as e:
    log.error("failed to fetch", e)
    return None

def fetch_new_postings():
  global qsize

  # Get new postings from subreddits.
  submissions = []
  for sr in subreddits:
    # Fetch new postings for subreddit.
    r = session.get("https://www.reddit.com/r/" + sr + "/new.json",
                    headers={"User-agent": "SLING Bot 1.0"}, timeout=180)
    if r.status_code == 404:
      log.error("unknown subreddit:", sr)
      continue
    elif r.status_code == 429:
      reset = int(r.headers.get("x-ratelimit-reset", 60))
      log.info("scan rate limit", reset, "secs")
      time.sleep(reset)
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
      created = posting["created_utc"]
      redditdb[sid] = json.dumps(posting)

      ts = created + (midnight - created) * flags.arg.delay;
      queue.append(Submission(ts, posting))
      qsize += 1
      log.info("### [%d] %s %s %s" % (qsize, sr, sid, title))

last_check = 0
while True:
  try:
    now = time.time()
    if now - last_check >= flags.arg.interval:
      log.info("Scanning for new postings")
      fetch_new_postings()
      log.info("Scanning done")
      last_check = now

    for i in range(len(queue)):
      submission = queue[i]
      if submission is None: continue
      if submission.ts < now:
        posting = submission.posting
        sr = posting["subreddit"]
        sid = posting["name"]
        title = posting["title"]
        log.info("CHECK [%d] %s %s %s" % (qsize, sr, sid, title))

        # Check if posting has been removed.
        posting = fetch_posting(sid)
        if posting is None or \
          posting["removed_by_category"] or \
          posting["title"] == "[deleted by user]":
          log.info("REMOVED [%d] %s %s %s" % (qsize, sr, sid, title))
          del redditdb[sid]
        queue[i] = None
        qsize -= 1

    time.sleep(60)

  except KeyboardInterrupt as error:
    log.info("Stopped")
    sys.exit()

  except:
    traceback.print_exc(file=sys.stdout)
    time.sleep(60)

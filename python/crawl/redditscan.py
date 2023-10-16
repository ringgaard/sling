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
import queue
import requests
import sys
import time
import threading
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
session = requests.Session()

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
                      headers = {"User-agent": "SLING Bot 1.0"})
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

isdst = time.daylight and time.localtime().tm_isdst > 0
tzofs = time.altzone if isdst else time.timezone

def check_posting(ts, sid):
  # Wait until midpoint between now and midnight.
  now = time.time()
  midnight = math.ceil((ts - tzofs) / 86400) * 86400 + tzofs
  midpoint = (ts + midnight) / 2
  if midpoint > now: time.sleep(midpoint - now)

  # Check if posting has been removed.
  posting = fetch_posting(sid)
  if posting is None or \
     posting["removed_by_category"] or \
     posting["title"] == "[deleted by user]":
    log.info("REMOVED", sid)
    del redditdb[sid]

queue = queue.PriorityQueue()

def removal_checker():
  while True:
    # Get oldest posting from queue.
    task = queue.get()
    try:
      ts = task[0]
      sid = task[1]
      check_posting(ts, sid)
    except:
      traceback.print_exc(file=sys.stdout)
    queue.task_done()

threading.Thread(target=removal_checker, daemon=True).start()

def fetch_new_postings():
  # Get new postings from subreddits.
  submissions = []
  for sr in subreddits:
    # Fetch new postings for subreddit.
    r = session.get("https://www.reddit.com/r/" + sr + "/new.json",
                    headers = {"User-agent": "SLING Bot 1.0"})
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
      log.info("###", sr, sid, title)
      redditdb[sid] = json.dumps(posting)
      submissions.append((created, sid))

  submissions.sort()
  for s in submissions: queue.put(s)

while True:
  try:
    log.info("Scanning for new postings")
    fetch_new_postings()
    log.info("Scanning done")
    time.sleep(flags.arg.interval)

  except KeyboardInterrupt as error:
    log.info("Stopped")
    sys.exit()

  except:
    traceback.print_exc(file=sys.stdout)
    time.sleep(60)


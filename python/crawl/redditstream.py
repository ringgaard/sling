# Copyright 2022 Ringgaard Research ApS
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

"""Collect postings from from reddit stream."""

import praw
import json
import traceback
import requests
import sys
import time
import urllib
import sling
import sling.flags as flags

flags.define("--apikeys",
             default="local/keys/reddit.json",
             help="Reddit API key file")

flags.define("--redditdb",
             default="reddit",
             help="Reddit submissions database")

flags.define("--stream",
             default=None,
             help="File with subreddits to stream")

flags.parse()

# Read lists of monitored subreddits.
session = requests.Session()
streams = []
for filename in flags.arg.stream.split(","):
  with open(filename, "r") as f:
    for line in f.readlines():
      sr = line.strip().lower();
      pos = sr.find(' ')
      if pos != -1: sr = sr[:pos].strip()
      if len(sr) == 0 or sr[0] == '#': continue;
      streams.append(sr);
print("Stream", len(streams), "subreddits")

# Connect to reddit submission database.
redditdb = sling.Database(flags.arg.redditdb)

# Connect to Reddit.
with open(flags.arg.apikeys, "r") as f:
  apikeys = json.load(f)

reddit = praw.Reddit(client_id=apikeys["client_id"],
                     client_secret=apikeys["client_secret"],
                     user_agent=apikeys["user_agent"],
                     check_for_updates=False)
reddit.read_only = True

# Fetch submission and store in database.
def fetch_submission(id):
  try:
    # Fetch submission from Reddit.
    url = "https://api.reddit.com/api/info/?id=" + id;
    headers = {"User-agent": apikeys["user_agent"]}
    r = session.get(url, headers=headers)
    r.raise_for_status()
    root = r.json()
    data = root["data"]["children"][0]["data"]

    # Save submission in database.
    return redditdb.add(id, json.dumps(data))
  except:
    traceback.print_exc(file=sys.stdout)

# Monitor live Reddit submission stream for postings.
multisr = '+'.join(streams)
while True:
  try:
    for submission in reddit.subreddit(multisr).stream.submissions():
      result = fetch_submission(submission.name)
      print("###",
            str(submission.subreddit),
            submission.name,
            "NSFW" if submission.over_18 else "",
            "DUP" if result != sling.DBNEW else "",
            submission.url,
            submission.title)

    print("restart submission stream")
    time.sleep(20)

  except KeyboardInterrupt as error:
    print("Stopped")
    sys.exit()

  except:
    traceback.print_exc(file=sys.stdout)
    time.sleep(60)


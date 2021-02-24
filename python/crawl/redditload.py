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

"""Retrieve all submissions to subreddit from pushshift.io."""

import requests
import json
import datetime
import time
import sling.flags as flags

flags.define("--subreddit",
             help="Subreddit to retrieve submissions from")

flags.define("--outdir",
             default=None,
             help="Output directory")

flags.define("--redditdb",
             default=None,
             help="Reddit submission database")

flags.parse()

session = requests.Session()
baseurl = "http://api.pushshift.io/reddit/search/submission"
batchsize = 100
if flags.arg.outdir != None:
  fout = open(flags.arg.outdir + "/" + flags.arg.subreddit.lower(), "w")
else:
  fout = None

num_submissions = 0
dt = None
after = 0
while True:
  # Fetch next batch.
  url = "%s/?subreddit=%s&sort=asc&after=%d&size=%d" % (
    baseurl, flags.arg.subreddit, after, batchsize)

  r = session.get(url)
  if r.status_code == 429:
    # Throttle down for rate limiting.
    print("\nthrottle down...", end='')
    time.sleep(60)
    print(" resume...")
    continue
  if not r.ok:
    print("\nHTTP error", r.status_code)
    time.sleep(5)
    continue

  # Write batch to file.
  result = r.json()
  submissions = result["data"]
  if len(submissions) == 0: break
  created = 0
  for submission in result["data"]:
    created = submission["created_utc"]
    if fout != None:
      fout.write(json.dumps(submission))
      fout.write("\n")
    if flags.arg.redditdb != None:
      key = "t3_" + submission["id"]
      dburl = flags.arg.redditdb + "/" + key
      r = session.put(dburl,
                      headers={"Mode": "add"},
                      data=json.dumps(submission))
      r.raise_for_status()
    num_submissions += 1

  dt =  datetime.datetime.fromtimestamp(created)
  print(flags.arg.subreddit, dt, num_submissions, "submissions", end='\r')

  # Resume at the creation time for last retrieved submission.
  after = created

if fout != None: fout.close()
print(flags.arg.subreddit, dt, num_submissions, "submissions")

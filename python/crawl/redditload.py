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
import sling
import sling.flags as flags

flags.define("--subreddit",
             help="Subreddit to retrieve submissions from")

flags.define("--output",
             default=None,
             help="Output record file")

flags.define("--outdir",
             default=None,
             help="Output directory")

flags.define("--redditdb",
             default=None,
             help="Reddit submission database")

flags.define("--ids",
             default=None,
             help="Reddit submission ids")

flags.parse()

session = requests.Session()
baseurl = "https://api.pushshift.io/reddit/search/submission"
batchsize = 100

if flags.arg.output != None:
  output = sling.RecordWriter(flags.arg.output)
else:
  output = None

if flags.arg.outdir != None:
  fout = open(flags.arg.outdir + "/" + flags.arg.subreddit.lower(), "w")
else:
  fout = None

if flags.arg.redditdb != None:
  redditdb = sling.Database(flags.arg.redditdb)
else:
  redditdb = None

num_submissions = 0
num_dups = 0
dt = None
before = int(time.time())
while True:
  # Fetch next batch.
  if flags.arg.ids:
    url = "%s/?ids=%s" % (baseurl, flags.arg.ids)
  else:
    url = "%s/?subreddit=%s&before=%d&size=%d" % (
      baseurl, flags.arg.subreddit, before, batchsize)

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
    key = "t3_" + submission["id"]
    if output != None:
      output.write(key, json.dumps(submission))
    if fout != None:
      fout.write(json.dumps(submission))
      fout.write("\n")
    if redditdb != None:
      res = redditdb.put(key, json.dumps(submission), mode=sling.DBADD)
      if res == sling.DBEXISTS: num_dups += 1
    num_submissions += 1

  dt =  datetime.datetime.fromtimestamp(created)
  print(flags.arg.subreddit, dt, num_submissions, "submissions", end='\r')

  # Resume at the creation time for last retrieved submission.
  if flags.arg.ids: break
  before = created

if output != None: output.close()
if fout != None: fout.close()
if redditdb != None: redditdb.close()
print(flags.arg.subreddit, dt, num_submissions, "submissions", num_dups, "dups")


# Retrieve all submissions to subreddit from pushshift.io.

import requests
import json
import datetime
import time
import sling.flags as flags

flags.define("--subreddit",
             help="Subreddit to retrieve submissions from")

flags.define("--outdir",
             default=".",
             help="Output directory")

flags.parse()

session = requests.Session()
baseurl = "http://api.pushshift.io/reddit/search/submission"
batchsize = 100

fout = open(flags.arg.outdir + "/" + flags.arg.subreddit.lower(), "w")
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
  r.raise_for_status()

  # Write batch to file.
  result = r.json()
  submissions = result["data"]
  if len(submissions) == 0: break
  created = 0
  for submission in result["data"]:
    created = submission["created_utc"]
    fout.write(json.dumps(submission))
    fout.write("\n")
    num_submissions += 1

  dt =  datetime.datetime.fromtimestamp(created)
  print(flags.arg.subreddit, dt, num_submissions, "submissions", end='\r')

  # Resume at the creation time for last retrieved submission.
  after = created

print(flags.arg.subreddit, dt, num_submissions, "submissions")


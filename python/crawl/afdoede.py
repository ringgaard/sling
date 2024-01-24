# Copyright 2024 Ringgaard Research ApS
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

"""Collect obituaries from afdoede.dk."""

import re
import json
import time
import urllib3
import sys
import sling
import sling.flags as flags
import sling.util

flags.define("--start",
             help="First mindeid to fetch",
             type=int)

flags.define("--end",
             help="Last mindeid to fetch",
             type=int)

flags.define("--margin",
             help="Number of missing obituaries before terminating",
             default=20,
             type=int)

flags.define("--db",
             default="afdoede",
             help="Obituary database")

flags.define("--delay",
             help="Delay between requests",
             default=1,
             type=float,
             metavar="SECS")

flags.define("--checkpoint",
             help="file with latest checkpoint for scanning afdoede.dk",
             default=None,
             metavar="FILE")

flags.parse()

pool =  urllib3.PoolManager()
db = sling.Database(flags.arg.db)
chkpt = sling.util.Checkpoint(flags.arg.checkpoint)

months = {
  "januar": 1,
  "februar": 2,
  "marts": 3,
  "april": 4,
  "maj": 5,
  "juni": 6,
  "juli": 7,
  "august": 8,
  "september": 9,
  "oktober": 10,
  "november": 11,
  "december": 12,

  "spetember": 9,
  "febuar": 2,
}

name_pat = re.compile(r"<h1 class=\"mb-0\">([^<]+)<\/h1>")
dates_pat = re.compile(r"<small>([^<]+)<\/small>")
annoucement_pat = re.compile(r"<div id=\"primaryAnnouncementText\" class=\"mt-1\" style=\"display: none;\">")

def find_date(text, year):
  if text == None: return None
  m = re.search("(\d+)\. (\w+) " + str(year), text)
  if m == None: return None
  yr = int(year)
  mo = months.get(m[2].lower())
  da = int(m[1])
  if yr < 1800 or yr > 2030: return None
  if mo is None: return None
  if da < 1 or da > 31: return None

  return yr * 10000 + mo * 100 + da

def fetch_obituary(mindeid):
  # Fetch page from afdoede.dk.
  headers = {"User-agent": "SLING Bot 1.0"}
  url = "https://afdoede.dk/minde/" + str(mindeid)
  r = pool.request("GET", url, headers=headers, timeout=60)
  if r.status != 200:
    print("error", r.status, url)
    return None
  html = r.data.decode()

  # Parse name.
  name = None
  m = name_pat.search(html)
  if m == None:
    print("No name match")
  else:
    name = m[1]

  # Parse birth and death dates.
  dob = None
  dod = None
  m = dates_pat.search(html)
  if m == None:
    print("No date match")
  else:
    dates = m[1]
    dash = dates.find("-")
    dob = dates[:dash].strip()
    dod = dates[dash + 1:].strip()

  if dob is None or dob == "N/A" or dod is None or dod == "N/A":
    print("missing dates", dob, dod);
    return None

  # Parse obituary announcement.
  obituary = None
  m = annoucement_pat.search(html)
  if m == None:
    print("No announcement match")
  else:
    begin = m.end(0)
    end = html.find("</div>", begin)
    obituary = html[begin:end].strip()
    obituary = obituary.replace("<p>", "")
    obituary = obituary.replace("</p>", "\n")
    obituary = obituary.replace("<br />", "\n")

  # Try to find exact dates in obituary.
  birth = find_date(obituary, dob)
  if birth == None: birth = int(dob)
  death = find_date(obituary, dod)
  if death == None: death = int(dod)

  return {
    "mindeid": str(mindeid),
    "name": name,
    "birth": birth,
    "death": death,
    "obituary": obituary,
  }

mindeid = flags.arg.start
if mindeid is None and chkpt.checkpoint != 0: mindeid = chkpt.checkpoint
last = None
missing = 0
while True:
  print("fetch", mindeid)
  minde = fetch_obituary(mindeid)
  if minde != None:
    last = mindeid
    print(minde)
    db[minde["mindeid"]] = json.dumps(minde)
    missing = 0
  else:
    missing += 1

  if flags.arg.end == None:
    if missing == flags.arg.margin: break
  else:
    if mindeid == flags.arg.end: break

  sys.stdout.flush()
  time.sleep(flags.arg.delay)
  mindeid += 1

if flags.arg.start is None and last != None:
  chkpt.commit(last + 1)


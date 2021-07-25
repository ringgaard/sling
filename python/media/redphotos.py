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
import datetime
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

flags.define("--aliases",
             help="phrase table for matching item names",
             default="data/e/kb/en/phrase-table.repo",
             metavar="FILE")

flags.define("--report",
             help="HTML report for unmatched postings",
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
  "AOC",
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
  "JuiceWRLD",
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
  "(", "[", ",", " - ", "|", "/", ":", "!", " – ", ";", "'s ", "’s ",
  " circa ", " c.",
  "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
  " by ", " is ", " was ", " in ", " on ", " with ", " at ", " as ", " from ",
  " for ",
  " aka ", " has ", " having ", " performing ", " during ", " being ",
  " posing ", " photographed ", " dressed ",
]

# HTML templates for unmatched postings.
html_report_header = """
<html>
<head>
<meta charset="utf-8">
<style>
body {
  font-family: Helvetica,sans-serif;
}
a {
  text-decoration: none;
}
.title {
  font-size: 20px;
}
.photo {
  margin: 3;
}
.descr {
  margin: 3px;
  line-height: 1.3;
}
.nsfw {
  border-radius: 3px;
  border: 1px solid;
  font-size: 12px;
  padding: 2px 4px;
  margin: 2px;
  color: #d10023;
}
.sfw {
  display: none;
}
</style>
</head>
<body>
"""

html_report_footer = """
</body>
</html>
"""

html_report_headline = """
  <h1><a href="https://www.reddit.com/r/{sr}/">{sr}</a></h1>
"""

html_report_template = """
<div style="display: flex">
  <div class="photo">
    <a href="{url}"><img src="{thumb}" width=70 height=70></a>
  </div>
  <div class="descr">
    <div class="title">{title}</div>
    <div>
      <span class="{marker}">NSFW</span>
      <a href="https://www.reddit.com{permalink}">{sid}</a>
      {xpost}
    </div>
    <div>
      {match}
    </div>
  </div>
</div>
"""

html_single_match = """
  <b>{query}</b>:
  <a href="https://ringgaard.com/kb/{itemid}?nsfw=1">{itemid}</a>
"""

html_multi_match = "{count} matches for <b>{query}</b>"

html_xpost = """
  cross-post from <a href="https://www.reddit.com{permalink}">{xsr}</a>
"""

# Initialize commons store.
commons = sling.Store()
n_subreddit = commons["subreddit"]
n_title = commons["title"]
n_url = commons["url"]
n_permalink = commons["permalink"]
n_is_self = commons["is_self"]
n_over_18 = commons["over_18"]
n_thumbnail = commons["thumbnail"]
n_preview = commons["preview"]
n_images = commons["images"]
n_resolutions = commons["resolutions"]
n_crosspost = commons["crosspost_parent_list"]
aliases = sling.PhraseTable(commons, flags.arg.aliases)
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
sr_reports = {}
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
  query = title
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
      query = name
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
  nsfw = posting[n_over_18]
  if itemid is None:
    if not selfie(title):
      print(sr, key, "UNKNOWN", title, "NSFW" if nsfw else "", url)
      if sr not in sr_reports: sr_reports[sr] = []

      # Get thumbnail image.
      thumb = posting[n_thumbnail]
      if thumb == "nsfw": thumb = None
      if thumb is None:
        thumb = posting
        for p in [n_preview, n_images, 0, n_resolutions, 0, n_url]:
          if p not in thumb:
            thumb = ""
            break
          thumb = thumb[p]

      # Check for alias matches.
      matches = aliases.query(query)
      match = ""
      if len(matches) == 1:
        match = html_single_match.format(itemid=matches[0].id(), query=query)
      elif len(matches) > 0:
        match = html_multi_match.format(count=len(matches), query=query)

      # Check for cross-posting.
      xpost = ""
      xpost_list = posting[n_crosspost]
      if xpost_list and len(xpost_list) == 1:
        xposting = xpost_list[0]
        xpost = html_xpost.format(
          permalink=xposting[n_permalink],
          xsr=xposting[n_subreddit],
        )

      # Add section to report for unknown posting.
      sr_reports[sr].append(html_report_template.format(
        url=url,
        thumb=thumb.replace("&amp;", "&"),
        title=title,
        marker="nsfw" if nsfw else "sfw",
        permalink=posting[n_permalink],
        sid=key,
        xpost=xpost,
        match=match,
      ))
    continue

  # Output photo to batch list.
  batch.write("%s %s #\t %s %s%s\n" %
              (sr, title, itemid, url, " NSFW" if nsfw else ""))

  print(sr, key, itemid, title, "NSFW" if nsfw else "", url)

chkpt.commit(redditdb.position())
redditdb.close()
batch.close()

# Output HTML report with unmatched postings.
if flags.arg.report:
  reportfn = datetime.datetime.now().strftime(flags.arg.report)
  report = open(reportfn, "w")
  report.write(html_report_header)
  for sr in sorted(sr_reports.keys()):
    report.write(html_report_headline.format(sr=sr))
    for line in sr_reports[sr]:
      report.write(line)
  report.write(html_report_footer)
  report.close()


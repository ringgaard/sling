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

"""Collect profiles for celebrities from /r/CelebrityAlbums."""

import json
import re
import requests
import urllib.parse
import os.path
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

flags.define("--celebdb",
             help="database for celebrity profiles",
             default="celeb",
             metavar="DB")

flags.define("--albums",
             help="output file for photo albums",
             default=None,
             metavar="FILE")

flags.parse()

# Initialize global store.
commons = sling.Store()
n_id = commons["id"]
n_name = commons["name"]
n_instance_of = commons["P31"]
n_human = commons["Q5"]
n_english = commons["/lang/en"]

n_subreddit = commons["subreddit"]
n_data = commons["data"]
n_children = commons["children"]
n_title = commons["title"]
n_body = commons["body"]
n_url = commons["url"]
n_over_18 = commons["over_18"]

# Link mapping for xrefs.
idmapping = [
  (commons["P345"], r"https://www.imdb.com/name/(.+)/"),
  (commons["P345"], r"https://imdb.com/name/(.+)/"),

  (commons["P2002"], r"https://twitter.com/(.+)"),
  (commons["P2002"], r"https://www.twitter.com/(.+)"),
  (commons["P2002"], r"https://mobile.twitter.com/(.+)"),

  (commons["P2003"], r"https://www.instagram.com/(\w+)/?"),
  (commons["P2003"], r"https://instagram.com/(\w+)/?"),

  (commons["P7085"], r"https://www.tiktok.com/@(.+)"),
  (commons["P7085"], r"https://www.tiktok.com/discover/(.+)"),

  (commons["P2397"], r"https://www.youtube.com/channel/(.+)"),
  ("YouTube user", r"https://www.youtube.com/user/(.+)"),
  ("YouTube", r"https://www.youtube.com/(.+)"),

  (commons["P8604"], r"https://onlyfans.com/(.+)"),

  (commons["P4015"], r"https://vimeo.com/(.+)"),

  (commons["P3943"], r"https?://(\w+)\.tumblr\.com"),
  (commons["P5246"], r"https://www.pornhub.com/pornstar/(.+)"),
  (commons["P7737"], r"https://open.spotify.com/artist/(.+)"),
  (commons["P1902"], r"https://www.deviantart.com/(.+)"),
  ("MyFreeCams", r"https://profiles.myfreecams.com/(.+)"),
]

linkpat = re.compile("\[(.+)\]\((.+)\)")

# Build xref priority table.
idpriority = {}
for prop, _ in idmapping:
  if type(prop) is str: continue
  if prop in idpriority: continue
  prio = len(idpriority) + 1
  idpriority[prop] = prio

commons.freeze()

# Wikipedia to QID mapping tables.
wikipedia_mappings = {}

# Map Wikipedia article id to QID.
def map_wikipedia(lang, article):
  mapping = wikipedia_mappings.get(lang)
  if mapping is None:
    fn = "data/e/wiki/" + lang + "/mapping.sling"
    if not os.path.isfile(fn): return None
    print("Loading wikipedia mapping for", lang)
    mapping = sling.Store()
    mapping.load(fn)
    mapping.freeze()
    wikipedia_mappings[lang] = mapping
  f = mapping["/wp/" + lang + "/" + article]
  if f is None: return None
  return f["/w/item/qid"]

# Open databases.
celebdb = sling.Database(flags.arg.celebdb)
redditdb = sling.Database(flags.arg.redditdb)
albums = None
if flags.arg.albums != None: albums = open(flags.arg.albums, "w")

# Load xref table.
print("Load xref table")
xrefs = sling.Store()
xrefs.load("data/e/kb/xrefs.sling")
xrefs.freeze()

# Find new submissions to /r/CelebrityAlbums.
print("Fetch new submissions")
chkpt = sling.util.Checkpoint(flags.arg.checkpoint)
session = requests.Session()
for id, version, value in redditdb(chkpt.checkpoint):
  store = sling.Store(commons)
  posting = store.parse(value, json=True)
  if posting[n_subreddit] != "CelebrityAlbums": continue

  # Fetch submission and comments from Reddit.
  sid = id[3:]
  url = "https://www.reddit.com/r/CelebrityAlbums/comments/%s.json" % sid
  r = session.get(url, headers={"User-agent": "SLING Bot 1.0"})
  r.raise_for_status()
  posting = store.parse(r.content, json=True)

  if len(posting) != 2 or len(posting[1][n_data][n_children]) < 1:
    print("no comment for", sid)
    continue

  submission = posting[0][n_data][n_children][0][n_data]
  comment = posting[1][n_data][n_children][0][n_data]

  # Get album url.
  url = submission[n_url]
  nsfw = submission[n_over_18]

  # The title is the name of the entity.
  title = submission[n_title]

  # Get text for first comment.
  body = comment[n_body]
  if type(body) is bytes:
    print("Bad body", body)
    continue

  # Find all links in first comment to collect xrefs and QIDs.
  print("=== Posting", sid, title, "(", url, ")")
  qid = None
  refs = {}
  key = None
  prio = None
  for m in re.finditer(linkpat, body):
    label = m[1]
    link = m[2]

    # Remove translation wrapper.
    if link.startswith("https://translate.google.com/translate?"):
      q = urllib.parse.parse_qs(link)
      link = q["u"][0]

    # Get Wikipedia article name.
    lang = None
    m = re.match(r"https://(\w+).wikipedia.org/wiki/(.+)", link)
    if m != None:
      # Map Wikipedia article name to QID.
      lang = m[1]
      article = urllib.parse.unquote(m[2])
      item = map_wikipedia(lang, article)
      if item is None:
        print("  ", "Unknown wikipedia id", lang, article)
        continue
      else:
        qid = item.id
        key = qid
        prio = 0
        print("QID", qid, lang, article)
        continue

    # Try to match links to properties.
    pos = link.find('?')
    if pos != -1: link = link[:pos]
    property = None
    identifier = None
    for mapping in idmapping:
      m = re.match(mapping[1], link)
      if m != None:
        property = mapping[0]
        identifier = m[1]
        break
    if property is None:
      print("Unknown", label, link)
    elif type(property) is str:
      print("Untracked", label, link)
    else:
      refs[property] = identifier
      if key is None or idpriority[property] < prio:
        key = property.id + "/" + identifier
        prio = idpriority[property]

  # Discard if no key found.
  if key is None:
    print("No key for profile", refs)
    continue

  # Try to map key to main key in xref table.
  if qid is None:
    main = xrefs[key]
    if main != None:
      print("map key", key, "to", main.id)
      key = main.id
      if key.startswith("Q"): qid = key

  # Try to find existing profile.
  profile = None
  data = celebdb[key]
  if data != None: profile = store.parse(data)

  if profile is None or profile.id != qid:
    # Create new profile.
    slots = []
    if qid != None: slots.append((n_id, qid))
    slots.append((n_name, store.qstr(title, n_english)))
    slots.append((n_instance_of, n_human))
    for property, identifier in refs.items():
      slots.append((property, identifier))
    profile = store.frame(slots)
  else:
    # Update existing profile.
    print("Update existing profile", key)
    if profile[n_id] is None and qid != None:
      profile.append(n_id, qid)
    if title != str(profile[n_name]):
      print("Name mismatch", title, "vs", profile[n_name])
    for property, identifier in refs.items():
      if identifier != profile[property]:
        print("Add new identifier", property.id, identifier)
        profile.append(property, identifier)

  # Save profile.
  print(key, "profile:", profile.data())
  celebdb[key] = profile.data(binary=True)

  # Output album to photo batch.
  if albums:
    albums.write("%s #\t %s %s%s\n" %
                 (title, key, url, " NSFW" if nsfw else ""))

# Cleanup
chkpt.commit(redditdb.position())
celebdb.close()
redditdb.close()
if albums: albums.close()


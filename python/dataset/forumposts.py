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

"""Extract posting threads from vBulletin-based fora."""

import re
import requests
import html
import urllib.parse
import sling
import sling.flags as flags

# Flags.
flags.define("--url")
flags.define("--first", type=int, default=1)
flags.define("--last", type=int, default=1)
flags.define("--db", default="vault/forum")
flags.define("--forum", default="vef")
flags.define("--images", default=False, action="store_true")
flags.define("--debug", default=False, action="store_true")
flags.parse()

# Frame store.
store = sling.Store()
n_id = store["id"]
n_name = store["name"]
n_alias = store["alias"]
n_description = store["description"]
n_media = store["media"]
n_other_name = store["P2561"]
n_described_at_url = store["P973"]
n_instance_of = store["P31"]
n_forum_post = store["Q57988118"]
n_imdb = store["P345"]
n_iafd = store["P3869"]
n_egafd = store["P8767"]
n_instagram = store["P2003"]

# Regex patterns.
link_pat = re.compile(r'<a href="showthread\.php\?[^"]+" id="thread_title_(\d+)">([^<]*)<\/a>')
comment_pat = re.compile(r'<td class="alt1" id="td_threadtitle_\d+" title="(.*)')
image_pat = re.compile(r'https?:\/\/[^ ]+\.(?:png|jpg|jpeg|gif)', re.IGNORECASE)

property_patterns = {
 n_imdb: re.compile(r'https?:\/\/(?:www\.)?imdb\.com\/name\/([^\/\?]+)'),
 n_iafd: re.compile(r'https?:\/\/www\.iafd\.com\/person\.rme\/perfid=(\w+)\/?'),
 n_egafd: re.compile(r'https?:\//www\.egafd\.com\/actresses\/details.php\/id\/(\w\d+)'),
 n_instagram: re.compile(r'https?:\/\/(?:www\.)?instagram\.com\/([^\/\?]+)')
}

# AKA prefix parsing.
aka_prefixes = [
  "AKAs -",
  "AKA -",
  "AKA-",
  "AKA-",
  "AKA ",
  "AKA:",
  "Performer AKA",
  "aka",
  "a.k.a.",
  "Aka:",
  "Also known as:",
]

aka_delimiters = [
  "|",
  " - ",
  " aka ",
  ",",
  "/",
]

# Parse forum overview page.
def parse_forum_page(html):
  block = []
  inhdr = True

  for line in html.split("\n"):
    line = line.strip()
    if len(line) == 0: continue
    if inhdr:
      if line == "<!-- show threads -->": inhdr = False
    elif line == "</tr><tr>":
      yield block
      block.clear()
    else:
      block.append(line)
  yield block

# Trim name.
def trim_name(name):
  for delim in ["(", "[", "@"]:
    delim = name.find(delim)
    if delim != -1: name = name[:delim]
  return name.strip(" \t.,;:")

# Split and trim list of names.
def split_names(names, delimiters):
  parts = []
  for d in delimiters:
    if d in names:
      for n in names.split(d):
        n = trim_name(n)
        if len(n) > 0 and n not in parts: parts.append(n)
      return parts

  n = trim_name(names)
  if len(n) > 0: parts.append(n)
  return parts

db = sling.Database(flags.arg.db)
num_threads = 0
for page in range(flags.arg.first, flags.arg.last + 1):
  print("page", page)

  # Fetch forum postings page.
  url = flags.arg.url + "&page=" + str(page)
  u = urllib.parse.urlparse(url)
  baseurl = u.scheme + "://" + u.netloc
  r = requests.get(url)

  # Parse out each forum thread as a separate block.
  for block in parse_forum_page(r.content.decode(errors="ignore")):
    sticky = False
    threadid = None
    title = None
    comment = []
    incomment = False

    #if flags.arg.debug: print("=== block ===\n", "\n".join(block))

    # Parse posting thread info.
    for line in block:
      m = link_pat.match(line)
      if m:
        threadid = m[1]
        title = m[2]

      m = comment_pat.match(line)
      if m:
        if m[1].endswith('">'):
          comment.append(m[1][:-2])
          incomment = False
        else:
          comment.append(m[1])
          incomment = True
      elif incomment:
        if line.endswith('">'):
          comment.append(line[:-2])
          incomment = False
        else:
          comment.append(line)

      if line == "Sticky:":
        sticky = True

    # Skip sticky threads.
    if sticky: continue

    # Skip untitled threads.
    if title is None: continue

    # Get names and links
    names = split_names(html.unescape(title), ["|", "/"])
    media = []
    props = {}

    description = ""
    for line in comment:
      line = html.unescape(line)
      if line.endswith("..."): line = line[:-3]
      if len(line) == 0: continue

      # Match image urls in comment line.
      urls = image_pat.findall(line)
      if len(urls) > 0:
        for u in urls:
          media.append(u)
          line = line.replace(u, "")
        continue

      # Match aliases in comment line.
      for aka in aka_prefixes:
        if not line.startswith(aka): continue
        aliases = split_names(line[len(aka):], aka_delimiters)
        names.extend(aliases)
        line = ""
        break

      # Match social links.
      for prop, urlpat in property_patterns.items():
        m = urlpat.match(line)
        if m:
          props[prop] = m[1]
          line = ""
          break

      line = line.strip()
      if len(line) > 0:
        if len(description) > 0:
          description += "; " + line
        else:
          description = line

    # Output item for thread.
    itemid = "/forum/" + flags.arg.forum + "/" + str(threadid)
    threadurl = baseurl + "/showthread.php?t=" + str(threadid)
    slots = [(n_id, itemid)]

    first = True
    for name in names:
      if first:
        slots.append((n_name, name))
      else:
        slots.append((n_alias, name))
        slots.append((n_other_name, name))
      first = False

    slots.append((n_description, "forum thread"))
    slots.append((n_instance_of, n_forum_post))
    slots.append((n_described_at_url, threadurl))
    for p, v in props.items():
      slots.append((p, v))
    if flags.arg.images:
      for m in media:
        slots.append((n_media, m))

    if flags.arg.debug:
      print("=========")
      print("thread:", threadid)
      print("url:", threadurl)
      print("title:", title)
      print("names:", names)
      if len(media) > 0: print("media:", media)
      if len(props) > 0: print("props", props)
      if len(description) > 0: print(description)
    else:
      print("thread", threadid, ":", title)

      frame = store.frame(slots)
      if itemid in db:
        print(itemid, "already in forum db")
      else:
        db[itemid] = frame.data(binary=True)
        num_threads += 1

print(num_threads, "forum threads")

db.close()

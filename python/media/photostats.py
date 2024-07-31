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

"""Photo database statistics."""

from urllib.parse import urlparse
import re
import requests
import sling
import sling.flags as flags

flags.define("--profiles",
             help="statistics for photo profiles",
             default=False,
             action="store_true")

flags.define("--cases",
             help="statistics for cases",
             default=False,
             action="store_true")

flags.define("--published",
             help="statistics for published cases",
             default=False,
             action="store_true")

flags.define("--sites",
             help="statistics for photo sites",
             default=False,
             action="store_true")

flags.define("--large",
             help="Large profile size",
             default=1000,
             type=int,
             metavar="SIZE")

flags.parse()

num_profiles = 0
num_photos = 0
num_nsfw = 0
num_new = 0
num_nophotos = 0

store = sling.Store()
n_id = store["id"]
n_is = store["is"]
n_subject_of = store["P805"]
n_nsfw = store["Q2716583"]
n_has_quality = store["P1552"]
n_media = store["media"]
n_topics = store["topics"]
n_publish = store["publish"]

bin_size = 100
num_bins = 1000
profile_bins = [0] * num_bins
photo_bins = [0] * num_bins
large_profiles = {}
max_photos = 0
topic_names = {}
topics = set()
sites = {}

site_patterns = [
  re.compile(r"www\.(.*)"),
  re.compile(r"i\.(.*)"),
  re.compile(r"images\d*\.(.*)"),
  re.compile(r"img\d*\.(.*)"),
  re.compile(r"ist\d-\d\.(.*)"),
  re.compile(r"farm\d+\.(.*)"),
  re.compile(r".+\.(\w\w\w+\.\w+)"),
]

def add_media(media):
  global num_nsfw
  if type(media) is sling.Frame:
    if media[n_subject_of] == n_nsfw or media[n_has_quality] == n_nsfw:
      num_nsfw += 1
    url = media[n_is]
  else:
    url = media

  if url.startswith("!"):
    url = url[1:]
    num_nsfw += 1
  if flags.arg.sites:
    host = urlparse(url).netloc
    domain = host
    for pattern in site_patterns:
      m = pattern.fullmatch(domain)
      if m != None: domain = m.group(1)
    sites[domain] = sites.get(domain, 0) + 1

if flags.arg.profiles:
  photodb = sling.Database("vault/photo")
  for key, value in photodb.items():
    profile = store.parse(value)
    num_profiles += 1

    id = profile[n_id]
    if id is None: id = key
    topics.add(id)

    count = 0
    for m in profile(n_media):
      num_photos += 1
      count += 1
      add_media(m)

    b = int(count / bin_size)
    profile_bins[b] += 1
    photo_bins[b] += count
    if count > flags.arg.large: large_profiles[str(key)] = count;
    if count > max_photos: max_photos = count
  photodb.close()

if flags.arg.cases:
  casedb = sling.Database("vault/case")
  for key, value in casedb.items():
    casefile = store.parse(value)
    if n_topics not in casefile: continue
    if flags.arg.published and not casefile[n_publish]: continue

    for topic in casefile[n_topics]:
      num_profiles += 1
      count = 0

      redir = topic[n_is]
      if redir is None:
        num_new += 1
        topics.add(topic.id)
      elif type(redir) is str:
        topics.add(redir)
      else:
        topics.add(redir.id)

      has_photos = False
      for m in topic(n_media):
        num_photos += 1
        count += 1
        has_photos = True
        add_media(m)
      if not has_photos: num_nophotos += 1

      b = int(count / bin_size)
      profile_bins[b] += 1
      photo_bins[b] += count
      if count > flags.arg.large:
        large_profiles[topic.id] = count;
        topic_names[topic.id] = topic.name
      if count > max_photos: max_photos = count
  casedb.close()

print(num_profiles, "profiles")
pct = num_nsfw * 100 / num_photos
num_sfw = num_photos - num_nsfw
print("%d photos, %s sfw, %s nsfw (%f%%)" %
  (num_photos, num_sfw, num_nsfw, pct))
print(len(topics), "topics")
if num_new > 0: print(num_new, "new")
if num_nophotos > 0: print(num_nophotos, "without photos")

print(max_photos, "photos in largest profile")
print()

acc_profiles = 0
acc_photos = 0
for b in range(num_bins):
  if profile_bins[b] != 0:
    acc_profiles += profile_bins[b]
    acc_photos += photo_bins[b]
    print("%5d %6d %6.2f%% %6d %6.2f%%" % (
          (b + 1) * bin_size,
          profile_bins[b],
          acc_profiles / num_profiles * 100,
          photo_bins[b],
          acc_photos / num_photos * 100))

store = sling.Store()
items = []
for key in large_profiles.keys(): items.append(store[key])
r = requests.post("https://ringgaard.com/kb/stubs",
                  headers={"Content-type": "application/sling"},
                  data=store.array(items).data(binary=True))
store.parse(r.content)

print()
n = 1
for k, v in sorted(large_profiles.items(), key=lambda item: -item[1]):
  name = store[k].name
  if name is None: name = topic_names.get(k)
  print("%3d. %5d %s (%s)" % (n, v, name, k))
  n += 1

if flags.arg.sites:
  print()
  acc = 0
  for site, count in sorted(sites.items(), key=lambda item: -item[1]):
    if count < 2: break
    acc += count
    print("%6.2f%% %6.2f%% %5d %s" % (
      count / num_photos * 100,
      acc / num_photos * 100,
      count, site))


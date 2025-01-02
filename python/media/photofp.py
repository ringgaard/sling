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

"""Add fingerprints to photo fingerprint database."""

import json

import sling
import sling.flags as flags
import sling.media.photo as photo

flags.define("--profiles",
             help="add photo fingerprints from photo profiles",
             default=False,
             action="store_true")

flags.define("--cases",
             help="add photo fingerprints from published cases",
             default=False,
             action="store_true")

flags.define("--dryrun",
             help="do not update database",
             default=False,
             action="store_true")

flags.define("--fpcache",
             help="record file with cached photo fingerprints",
             default=None,
             metavar="RECFILE")

flags.parse()

fpdb = sling.Database(flags.arg.fpdb)

photo.load_photo_cache(flags.arg.fpcache)

num_fingerprints = 0

if flags.arg.profiles:
  num_profiles = 0
  for key, _, data in photo.photodb():
    num_profiles += 1
    new_photos = 0

    profile = photo.Profile(key, data)

    # Get fingerprints for photos in profile.
    fingerprints = fpdb[profile.urls()]

    # Add missing photo fingerprints.
    for url, fpdata in fingerprints.items():
      # Skip videos.
      if photo.is_video(url): continue

      # Parse fingerprint info.
      if fpdata is None:
        fpinfo = {"item": key}
      else:
        fpinfo = json.loads(fpdata)

      # Compute photo fingerprint.
      if flags.arg.hash not in fpinfo:
        p = photo.get_photo(key, url)
        if p is None: continue

        fpinfo["width"] = p.width
        fpinfo["height"] = p.height
        fpinfo[flags.arg.hash] = p.fingerprint

      # Add duplicate.
      if key != fpinfo["item"]:
        other = fpinfo.get("other")
        if other is None:
          other = []
          fpinfo["other"] = other
        if not key in other:
          other.append(key)
          print("=== dup", url, fpinfo)

      # Write fingerprint info to database.
      if not flags.arg.dryrun: fpdb[url] = json.dumps(fpinfo)
      new_photos += 1
      num_fingerprints += 1

    if new_photos > 0:
      print(num_profiles, key, new_photos, "new")

if flags.arg.cases:
  store = sling.Store()
  n_id = store["id"]
  n_is = store["is"]
  n_name = store["name"]
  n_publish = store["publish"]
  n_topics = store["topics"]
  n_media = store["media"]

  casedb = sling.Database("vault/case")
  for key, value in casedb.items():
    casefile = store.parse(value)
    if n_topics not in casefile: continue
    if not casefile[n_publish]: continue

    for topic in casefile[n_topics]:
      # Get item id.
      topicid = topic[n_id]
      redir = topic[n_is]
      if type(redir) is sling.Frame:
        itemid = redir.id
      else:
        itemid = redir
      if itemid is None: itemid = topicid

      # Get photo urls.
      urls = []
      for media in topic(n_media):
        url = store.resolve(media)
        if url.startswith('!'): url = url[1:]
        urls.append(url)

      # Get fingerprints for photos in topic.
      fingerprints = fpdb[urls]

      # Add missing photo fingerprints.
      new_photos = 0
      for url, fpdata in fingerprints.items():
        # Check for empty urls.
        if url is None:
          print("empty url in topic", topicid)
          continue

        # Skip videos.
        if photo.is_video(url): continue

        # Parse fingerprint info.
        if fpdata is None:
          fpinfo = {"item": itemid, "topic": topicid}
        else:
          fpinfo = json.loads(fpdata)

        # Compute photo fingerprint.
        if flags.arg.hash not in fpinfo:
          p = photo.get_photo(itemid, url)
          if p is None:
            print(topicid, "missing photo", url)
            continue

          fpinfo["width"] = p.width
          fpinfo["height"] = p.height
          fpinfo[flags.arg.hash] = p.fingerprint

        # Add duplicate.
        if itemid != fpinfo["item"] and topicid != fpinfo["item"]:
          other = fpinfo.get("other")
          if other is None:
            other = []
            fpinfo["other"] = other
          if not itemid in other:
            other.append(itemid)
            print("=== dup", url, fpinfo)

        # Write fingerprint info to database.
        if not flags.arg.dryrun: fpdb[url] = json.dumps(fpinfo)
        new_photos += 1
        num_fingerprints += 1

      if new_photos > 0:
        print(itemid, topic[n_name], new_photos, "new")

  casedb.close()

fpdb.close()
print(num_fingerprints, "new fingerprints added")

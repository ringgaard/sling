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

"""Add photos to photo database."""

import json
import requests
import os
import re
import sys
import traceback
import urllib.parse
import sling
import sling.flags as flags
import sling.media.photo as photo

flags.define("--id",
             default=None,
             help="item id photo updates")

flags.define("--caption",
             default=None,
             help="photo caption")

flags.define("--source",
             default=None,
             help="photo source")

flags.define("--nsfw",
             help="mark photos as nsfw",
             default=False,
             action="store_true")

flags.define("--overwrite",
             help="overwrite existing photos",
             default=False,
             action="store_true")

flags.define("-r", "--remove",
             help="remove photos",
             default=False,
             action="store_true")

flags.define("-x", "--exclude",
             help="exclude photos",
             action="append",
             nargs="+",
             metavar="URL")

flags.define("--delete",
             default=None,
             help="delete photos with match description",
             metavar="DESCRIPTION")

flags.define("--other",
             default=None,
             help="add photos from other profile")

flags.define("--truncate",
             help="truncate after first deleted photo",
             default=False,
             action="store_true")

flags.define("--dryrun",
             help="do not update database",
             default=False,
             action="store_true")

flags.define("--batch",
             default=None,
             help="batch file for bulk import")

flags.define("--dedup",
             help="remove duplicates from profile",
             default=False,
             action="store_true")

flags.define("--cont",
             help="continue on errors in batch mode",
             default=False,
             action="store_true")

flags.define("url",
             nargs="*",
             help="photo URLs",
             metavar="URL")

flags.parse()

# Sanity Check for (missing) profile id.
if flags.arg.id and flags.arg.id.startswith("http"):
  raise Exception("invalid id: " + flags.arg.id)

# Get excluded photos.
excluded = set()
if flags.arg.exclude != None:
  for url in flags.arg.exclude:
    excluded.add(url[0])

# Bulk load photos from batch file.
def bulk_load(batch):
  profiles = {}
  updated = set()
  fin = open(batch)
  num_new = 0
  num_photos = 0
  for line in fin:
    # Get id, url, and nsfw fields.
    tab = line.find('\t')
    if tab != -1: line = line[tab + 1:].strip()
    line = line.strip()
    if len(line) == 0: continue
    fields = line.split()
    id = fields[0]
    url = fields[1]
    nsfw = len(fields) >= 3 and fields[2] == "NSFW"

    # Get profile or create a new one.
    profile = photo.Profile(id)
    if profile.isnew: num_new += 1
    profiles[id] = profile
    print("*** PROFILE %s, %d existing photos" % (id, profile.count()))

    # Add media to profile.
    try:
      n = profile.add_media(url, flags.arg.caption, nsfw or flags.arg.nsfw)
      if n > 0:
        num_photos += n
        updated.add(id)
    except KeyboardInterrupt as error:
      sys.exit()
    except:
      if not flags.arg.cont: raise
      print("Error processing", url, "for", id)
      traceback.print_exc(file=sys.stdout)

  fin.close()

  # Write updated profiles.
  photo.store.coalesce()
  for id in updated:
    profile = profiles[id]
    if flags.arg.dedup: profile.dedup()
    if flags.arg.dryrun:
      print(profile.count(), "photos;", id, "not updated")
    else:
      print("Write", id, profile.count(), "photos")
      profile.write()

  print(len(profiles), "profiles,",
        num_new, "new,",
        len(updated), "updated,",
        num_photos, "photos")

if flags.arg.batch:
  # Bulk import.
  bulk_load(flags.arg.batch)
else:
  # Read existing photo profile for item.
  profile = photo.Profile(flags.arg.id)
  profile.excluded = excluded
  num_added = 0
  num_removed = 0
  if not profile.isnew: print(profile.count(), "exisiting photos")

  # Delete all existing media on overwrite mode.
  if flags.arg.overwrite:
    num_removed += profile.count()
    profile.clear()

  if flags.arg.other:
    # Add photos from other profile.
    other = photo.Profile(flags.arg.other)
    num_added += profile.copy(other)

  if flags.arg.remove or flags.arg.delete:
    # Remove media matching urls.
    keep = []
    truncating = False
    for media in profile.media():
      link = profile.url(media)

      remove = False
      if truncating:
        remove = True
      elif link in flags.arg.url:
        remove = True
      elif flags.arg.delete:
        caption = str(media[n_legend])
        if caption and flags.arg.delete in caption: remove = True

      if remove:
        print("Remove", link)
        if flags.arg.truncate: truncating = True
        num_removed += 1
      else:
        keep.append(media)

    profile.replace(keep)
  else:
    # Fetch photo urls.
    for url in flags.arg.url:
      num_added += profile.add_media(url, flags.arg.caption, flags.arg.nsfw)

  # Remove duplicates.
  if flags.arg.dedup: num_removed += profile.dedup()

  # Write profile.
  if flags.arg.dryrun:
    print(profile.count(), "photos;", flags.arg.id, "not updated")
  elif num_added > 0 or num_removed > 0:
    print("Write", flags.arg.id,
          profile.count(), "photos,",
          num_removed, "removed,",
          num_added, "added")
    photo.store.coalesce()
    profile.write()


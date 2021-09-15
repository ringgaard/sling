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

"""Find and remove duplicate photos."""

import hashlib
import requests
import sys
import sling
import sling.util
import sling.flags as flags
import sling.media.photo as photo

flags.define("--id",
             default=None,
             help="Item id photo updates")

flags.define("--all",
             help="check all profiles",
             default=False,
             action="store_true")

flags.define("--checkpoint",
             help="file with latest checkpoint for scanning photo db",
             default=None,
             metavar="FILE")

flags.define("--dryrun",
             help="do not update database",
             default=False,
             action="store_true")

flags.parse()

if flags.arg.all:
  # Get id of all profiles changed since last run.
  chkpt = sling.util.Checkpoint(flags.arg.checkpoint)
  ids = set(photo.photodb().keys(begin=chkpt.checkpoint))
  print(len(ids), "profiles to update")

  # Check all profiles.
  total_removed = 0
  for id in ids:
    profile = photo.Profile(id)
    removed = profile.dedup()
    if removed > 0 and not flags.arg.dryrun:
      # Write updated profile.
      profile.write()
      total_removed += 1

  if not flags.arg.dryrun:
    chkpt.commit(photo.photodb().epoch())
    print(total_removed, "photos removed")
else:
  # Check single profile.
  profile = photo.Profile(flags.arg.id)
  if profile.isnew:
    print("no profile found for", id)
  else:
    removed = profile.dedup()
    if removed > 0 and not flags.arg.dryrun:
      # Write updated profile.
      profile.write()


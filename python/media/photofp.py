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

flags.parse()

fpdb = sling.Database(flags.arg.fpdb)

num_profiles = 0
num_fingerprints = 0
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

    # Check if hash is already in fingerprint database.
    if flags.arg.hash in fpinfo: continue

    # Compute photo fingerprint.
    p = photo.get_photo(key, url)
    if p is None: continue

    fpinfo["width"] = p.width
    fpinfo["height"] = p.height
    fpinfo[flags.arg.hash] = p.fingerprint

    # Write fingerprint info to database.
    fpdb[url] = json.dumps(fpinfo)
    new_photos += 1
    num_fingerprints += 1

  if new_photos > 0:
    print(num_profiles, key, new_photos, "new")

fpdb.close()
print(num_fingerprints, "new fingerprints added")


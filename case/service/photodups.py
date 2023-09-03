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

"""Photo duplicate service"""

import json
import sling
import sling.media.photo as photolib

class DupsService:
  def handle(self, request):
    r = json.loads(request.body)
    itemid = r.get("itemid")
    images = r["images"]
    existing = r["existing"]

    # Try to preload image fingerprints from cache.
    photolib.load_fingerprints(images)
    photolib.load_fingerprints(existing)

    # Compute image hash for existing photos.
    photos = {}
    for url in existing:
      photo = photolib.get_photo(itemid, url)
      if photo is None: continue
      photos[photo.fingerprint] = photo

    # Compute image hash for each photo to detect duplicates.
    dups = []
    missing = []
    for url in images:
      # Skip videos.
      if photolib.is_video(url): continue

      # Get photo information.
      photo = photolib.get_photo(itemid, url)
      if photo is None:
        missing.append(url)
        continue

      # Check for duplicate.
      dup = photos.get(photo.fingerprint)
      if dup is not None:
        dups.append({
          "url": url,
          "width": photo.width,
          "height": photo.height,
          "bigger": photo.size() > dup.size(),
          "smaller": photo.size() < dup.size(),
          "dup": {
            "url": dup.url,
            "width": dup.width,
            "height": dup.height,
            "existing": dup.url in existing,
          }
        })

      # Add photo fingerprint for new or bigger photos.
      if dup is None or dup.size() < photo.size():
        photos[photo.fingerprint] = photo

    return {"dups": dups, "missing": missing}


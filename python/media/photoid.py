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

"""Build photo id table for mapping photo fingerprints to photo info."""

import json

import sling
import sling.flags as flags
import sling.media.photo as photo

flags.define("--output",
             help="output for photo id mapping",
             default="data/e/media/photoid.rec",
             metavar="FILE")

flags.parse()

def pixels(info):
  return info["width"] * info["height"]

fpdb = sling.Database(flags.arg.fpdb)
fingerprints = {}
for url, _, data in fpdb:
  info = json.loads(data)
  fingerprint = info.get(flags.arg.hash)
  if fingerprint is None: continue

  existing = fingerprints.get(fingerprint)
  if existing is None or pixels(existing) < pixels(info):
    fingerprints[fingerprint] = {
      "item": info["item"],
      "url": url,
      "width": info["width"],
      "height": info["height"],
    }

fpdb.close()

output = sling.RecordWriter(flags.arg.output, index=True)
for fp, info in fingerprints.items():
  output.write(fp, json.dumps(info))
output.close()
print(len(fingerprints), "fingerprints")


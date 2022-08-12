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

fpdb = sling.Database(flags.arg.fpdb)
output = sling.RecordWriter(flags.arg.output, index=True)

for url, _, data in fpdb:
  fp = json.loads(data)
  fingerprint = fp.get(flags.arg.hash)
  if fingerprint is None: continue

  info = {
    "item": fp["item"],
    "url": url,
    "width": fp["width"],
    "height": fp["height"],
  }
  output.write(fingerprint, json.dumps(info))

fpdb.close()
output.close()

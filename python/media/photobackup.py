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

"""Tool for backing up photos from media database."""

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

# 18014509171998720

import sling
import sling.util
import sling.flags as flags

flags.define("--mediadb",
             help="database for images",
             default="vault/media",
             metavar="DB")

flags.define("--output",
             help="output for backup",
             metavar="FILE")

flags.define("--checkpoint",
             help="file with latest checkpoint for scanning photo db",
             default="local/mediadump.chkpt",
             metavar="FILE")

flags.parse()

mediadb = sling.Database(flags.arg.mediadb, batch=16)
chkpt = sling.util.Checkpoint(flags.arg.checkpoint)
output = sling.RecordWriter(flags.arg.output, chunksize=0, compression=0)

num_images = 0
for url, timestamp, data in mediadb(begin=chkpt.checkpoint):
  if url.startswith("https://upload.wikimedia.org/"): continue

  print(num_images, url, len(data), timestamp)
  output.write(url, data, timestamp)
  num_images += 1

output.close()
chkpt.commit(mediadb.epoch())


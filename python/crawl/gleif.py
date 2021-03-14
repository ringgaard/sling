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

"""Fetch daily LEI files from gleif.org."""

import json
import requests
import sling
import sling.flags as flags

flags.define("--gleif",
             default="https://leidata-preview.gleif.org",
             help="Site for downloading GLEIF data")

flags.define("--dir",
             default="data/c/lei",
             help="Location for storing downloaded files")

flags.define("--type",
             default="xml",
             help="File type (xml, json, csv)")

flags.parse()

# Get publishing list.
r = requests.get(flags.arg.gleif + "/api/v2/golden-copies/publishes")
latest = r.json()["data"][0]

print("Published:", latest["publish_date"])
for dataset in ["lei2", "rr", "repex"]:
  # Get url and filename.
  f = latest[dataset]["full_file"][flags.arg.type]
  url = f["url"]
  fn = flags.arg.dir + "/" + dataset + "." + flags.arg.type + ".zip"

  # Download file.
  print("Download", url)
  r = requests.get(url, stream=True)
  r.raise_for_status()
  f = open(fn, "wb")
  for chunk in r.iter_content(chunk_size=8192): f.write(chunk)
  f.close()

print("Done.")

# Copyright 2024 Ringgaard Research ApS
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

"""Restore case user home directory from backup"""

import json
import sling.flags as flags

flags.define("--backup",
             help="backup file with user cases",
             default=None,
             metavar="FILE")

flags.define("--homedir",
             help="home directory of user",
             default=None,
             metavar="DIR")

flags.parse()

backup = json.load(open(flags.arg.backup))

index = open(flags.arg.homedir + "/index.json", "w")
directory = {}
for rec in backup["casedir"]: directory[rec["id"]] = rec
json.dump(directory, index)
index.close()

for case in backup["casedata"]:
  caseid = case["id"]
  data = bytes(case["data"])
  fn = "%s/%d.sling" % (flags.arg.homedir, caseid)
  f = open(fn, "wb")
  f.write(data)
  f.close()


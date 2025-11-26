# Copyright 2025 Ringgaard Research ApS
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

""" Extract obituaries and output items for indexing."""

import json
import sling

store = sling.Store()

n_id = store["id"]
n_name = store["name"]
n_description = store["description"]
n_type = store["P31"]
n_human = store["Q5"]
n_dob = store["P569"]
n_dod = store["P570"]
n_afdoede = store["PAFDO"]
n_summary = store["summary"]

# Extract obituaries from database.
db = sling.Database("vault/afdoede")
output = sling.RecordWriter("data/e/search/afdoede.rec", index=True)
db = sling.Database("vault/afdoede")
for data in db.values():
  rec = json.loads(data)
  mindeid = rec["mindeid"]
  name = rec["name"]
  birth = rec["birth"]
  death = rec["death"]
  obituary = rec["obituary"]

  topic = store.frame("PAFDO/" + mindeid)
  if name: topic[n_name] = name
  if birth and death:
    dob = sling.Date(birth)
    dod = sling.Date(death)
    topic[n_description] = str(dob.year) + "-" + str(dod.year)
  if birth: topic[n_dob] = birth
  if death: topic[n_dod] = death
  if obituary: topic[n_summary] = obituary.replace("\n", "<br>")
  topic[n_afdoede] = mindeid

  output.write(topic.id, topic.data(binary=True))

db.close()
output.close()

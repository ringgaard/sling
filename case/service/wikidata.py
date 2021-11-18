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

"""SLING Wikidata item service"""

import requests
import sling

class WikidataService:
  def __init__(self):
    self.commons = sling.Store()
    self.n_id = self.commons["id"]
    self.n_is = self.commons["is"]
    self.wikiconv = sling.WikiConverter(self.commons)
    self.commons.freeze()
    self.session = requests.Session()

  def handle(self, request):
    # Get QID for item.
    params = request.params()
    qid = params["qid"][0]
    print("fetch wikidata item for", qid)

    # Fetch item from wikidata site.
    url = "https://www.wikidata.org/wiki/Special:EntityData/" + qid + ".json"
    r = self.session.get(url)

    # Convert item to frame.
    store = sling.Store(self.commons)
    item, revision = self.wikiconv.convert_wikidata(store, r.content)

    # Create frame where id: is changed to is:.
    slots = []
    for name, value in item:
      if name == self.n_id:
        slots.append((self.n_is, item))
      else:
        slots.append((name, value))
    frame = store.frame(slots)

    return frame


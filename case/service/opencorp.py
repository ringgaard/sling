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

"""SLING case service for fetching company data from OpenCorporates"""

import json
import urllib3

import sling.log as log

class OpenCorpService:
  def __init__(self):
    self.apikey = None
    self.pool = urllib3.PoolManager()
    try:
      credentials = json.load(open("local/keys/opencorp.json"))
      self.apikey = credentials["apikey"]
    except:
      log.info("Using public OpenCorporates API")
      pass

  def handle(self, request):
    params = request.params()
    ocid = params["ocid"][0]
    log.info("fetch opencorp company", ocid)
    url = "https://api.opencorporates.com/companies/" + ocid
    if self.apikey: url += "?api_token=" + self.apikey
    r = self.pool.request("GET", url, timeout=60)
    if r.status != 200: return r.status
    return r.data


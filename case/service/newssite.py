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

"""Look up item ids for news sites"""

import json
import sling.flags as flags

flags.define("--newssites",
             default="data/crawl/newssites.txt",
             help="list of news sites")

url_prefixes = ["www.", "eu.", "uk.", "rss.", "rssfeeds.", "m."]

class NewsSite:
  def __init__(self, domain, qid, name, twitter=None, altdomain=None):
    self.domain = domain
    self.qid = qid
    self.name = name
    self.twitter = twitter
    self.altdomain = altdomain

class NewsSiteService:
  def __init__(self):
    # Load news site list.
    self.sites = {}
    with open(flags.arg.newssites, "r") as f:
      for line in f.readlines():
        line = line.strip()
        if len(line) == 0 or line[0] == '#': continue
        fields = line.split(",")
        if len(fields) < 3:
          print("too few fields for news site:", line)
          continue
        domain = fields[0]
        qid = fields[1]
        name = fields[2]

        twitter = None
        if len(fields) >= 4 and len(fields[3]) > 0:
          twitter = fields[3]

        altdomain = None
        if len(fields) >= 5:
          altdomain = fields[4]

        site = NewsSite(domain, qid, name, twitter, altdomain)
        self.sites[domain] = site
        if altdomain: self.sites[altdomain] = site
        if twitter: self.sites[twitter] = site

  def handle(self, request):
    # Get site name.
    params = request.params()
    sitename = params["site"][0]

    # Trim site name.
    for prefix in url_prefixes:
      if sitename.startswith(prefix): sitename = sitename[len(prefix):]

    # Look up site.
    site = self.sites.get(sitename)
    if not site: return {"sitename": sitename}

    return {
      "sitename": sitename,
      "domain": site.domain,
      "siteid": site.qid,
      "name": site.name,
      "twitter": site.twitter,
    }


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

"""SLING case system services"""

import importlib

class Service:
  def __init__(self, package, handler, ondemand=False):
    self.package = package
    self.handler = handler
    self.ondemand = ondemand
    self.module = None
    self.instance = None

  def load(self):
    self.module = importlib.import_module("." + self.package, "service")

  def init(self):
    self.instance = getattr(self.module, self.handler)()

services = {
  "albums": Service("albums", "AlbumService"),
  "article": Service("article", "ArticleService"),
  "biz": Service("biz", "BizService", True),
  "dups": Service("photodups", "DupsService"),
  "profile": Service("photodups", "ProfileService"),
  "newssite": Service("newssite", "NewsSiteService"),
  "opencorp": Service("opencorp", "OpenCorpService", True),
  "rdf": Service("rdf", "RDFService", True),
  "transcode": Service("transcode", "TranscodeService", True),
  "twitter": Service("twitter", "TwitterService", True),
  "viaf": Service("viaf", "VIAFService", True),
  "wikidata": Service("wikidata", "WikidataService", True),
}

def load():
  for service in services.values():
    if not service.ondemand: service.load()

def init():
  for service in services.values():
    if not service.ondemand: service.init()

def process(name, request):
  # Find service.
  service = services.get(name)
  if service is None: return 404;

  # Load and initialize on-demand service.
  if not service.module: service.load()
  if not service.instance: service.init()

  # Let service process the request.
  return service.instance.handle(request)


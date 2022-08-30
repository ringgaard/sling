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
  def __init__(self, package, handler):
    self.package = package
    self.handler = handler

  def load(self):
    self.module = importlib.import_module("." + self.package, "service")

  def init(self):
    self.instance = getattr(self.module, self.handler)()

services = {
  "albums": Service("albums", "AlbumService"),
  "article": Service("article", "ArticleService"),
  "newssite": Service("newssite", "NewsSiteService"),
  "transcode": Service("transcode", "TranscodeService"),
  "twitter": Service("twitter", "TwitterService"),
  "wikidata": Service("wikidata", "WikidataService"),
  "viaf": Service("viaf", "VIAFService"),
  "biz": Service("biz", "BizService"),
  "opencorp": Service("opencorp", "OpenCorpService"),
  "dups": Service("photodups", "DupsService"),
}

def load():
  for service in services.values(): service.load()

def init():
  for service in services.values(): service.init()

def process(name, request):
  # Find service.
  service = services.get(name)
  if service is None: return 404;

  # Let service process the request.
  return service.instance.handle(request)


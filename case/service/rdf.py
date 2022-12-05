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

"""SLING RDF service"""

# Install: sudo pip3 install PyLD

from collections import namedtuple
from pyld import jsonld
from urllib.parse import urljoin
import json
import requests
import sling
import sling.log as log

# Initialize commons store.
commons = sling.Store()
n_id = commons["id"]
n_is = commons["is"]
n_isa = commons["isa"]
commons.freeze()

# Install JSON-LD schema loader.

schema_cache = {}

def schema_loader(url, options={}):
  doc = schema_cache.get(url)
  if doc: return doc

  log.info("Load JSON-LD context", url)

  headers = options.get("headers")
  if headers is None:
    headers = {"Accept": "application/ld+json, application/json"}

  r = requests.get(url, headers=headers)

  content_type = r.headers.get("Content-Type")
  if not content_type: content_type = "application/octet-stream"

  if content_type != "application/ld+json":
    link_header = r.headers.get("Link")
    if link_header:
      link = jsonld.parse_link_header(link_header)
      for rel in ["http://www.w3.org/ns/json-ld#context", "alternate"]:
        linked_context = link.get(rel)
        if linked_context:
          redirect = urljoin(url, linked_context["target"])
          r = requests.get(redirect, headers=headers)
          content_type = r.headers.get("Content-Type")

  doc = {
    "contentType": content_type,
    "contextUrl": None,
    "documentUrl": r.url,
    "document": r.json()
  }
  schema_cache[url] = doc
  return doc

jsonld.set_document_loader(schema_loader)

# URI mapping table.
URIEntry = namedtuple("URIEntry", ["uri", "prop", "prefix", "suffix"])

class URIMapping:
  def __init__(self, xrefs):
    self.n_is = xrefs["is"]
    self.n_id = xrefs["id"]
    self.n_prefix = xrefs["prefix"]
    self.n_suffix = xrefs["suffix"]
    self.mappings = []
    for name, value in xrefs["/w/urimap"]:
      if name == self.n_id: continue
      uri = name
      prop = value
      prefix = ""
      suffix = ""
      if type(value) is sling.Frame:
        prop = value[self.n_is]
        prefix = value[self.n_prefix]
        suffix = value[self.n_suffix]
        if prefix is None: prefix = ""
        if suffix is None: suffix = ""
      self.mappings.append(URIEntry(uri, prop, prefix, suffix))
    self.mappings.sort(key=lambda e: e.uri)

  def locate(self, uri):
    # Bail out if there are no URI mappings.
    if len(self.mappings) == 0: return -1

    # Find mapping with prefix match.
    lo = 0
    hi = len(self.mappings) - 1
    while lo < hi:
      mid = (lo + hi) >> 1
      e = self.mappings[mid]
      if uri < e.uri:
        hi = mid
      else:
        lo = mid + 1

    # Check that the entry is a match for the URI.
    match = lo - 1
    if match < 0: return -1
    e = self.mappings[match]
    if not uri.startswith(e.uri): return -1
    if not uri.endswith(e.suffix): return -1

    return match

  def map(self, uri):
    match = self.locate(uri)
    if match == -1: return uri
    e = self.mappings[match]
    identifier = e.prefix + uri[len(e.uri):len(uri) - len(e.suffix)]
    if identifier.endswith("/"): identifier = identifier[:-1]
    if len(e.prop) == 0:
      return identifier
    else:
      return e.prop + "/" + identifier

# Load xrefs.
log.info("Loading cross-reference for RDF service")
xrefs = sling.Store()
xrefs.load("data/e/kb/xrefs.sling")
urimap = URIMapping(xrefs)
xrefs.freeze()

def xmap(uri):
  uri = urimap.map(uri)
  canonical = xrefs[uri]
  if canonical:
    return canonical.id
  else:
    return uri

# JSON-LD converter service.
class RDFService:
  def convert(self, store, obj, nested=False):
    slots = []
    for k, v in obj.items():
      if k == "@id":
        slots.append((n_is if nested else n_id, store[xmap(v)]))
      elif k == "@value":
        slots.append((n_is, v))
      elif k == "@type":
        if type(v) is list:
          for t in v:
            slots.append((n_isa, store[xmap(t)]))
        else:
          slots.append((n_isa, store[xmap(v)]))
      else:
        prop = store[xmap(k)]
        for e in v:
          if type(e) is dict:
            value = self.convert(store, e, True)
          else:
            value = store[xmap(e)]
          slots.append((prop, value))

    if len(slots) == 1 and (slots[0][0] == n_id or slots[0][0] == n_is):
      return slots[0][1]
    else:
      return store.frame(slots)

  def handle(self, request):
    # Get JSON-LD document.
    input = request.json()

    # Expand JSON-LD document into canonical form.
    doc = jsonld.expand(input)
    #print(json.dumps(doc, indent=2))

    # Convert JSON-LD to SLING format.
    output = []
    store = sling.Store(commons)
    for obj in doc:
      frame = self.convert(store, obj)
      output.append(frame)

    # Return array of SLING frames.
    return store.array(output)


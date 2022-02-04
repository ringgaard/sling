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

"""SLING Wikibase integration for exporting topics to Wikidata"""

import os
import json
import random
import re
import requests
import requests_oauthlib
import sling
import sling.log as log

class Credentials:
  def __init__(self, key, secret):
    self.key = key
    self.secret = secret

# Get WikiMedia application keys.
wikikeys = "local/keys/wikimedia.json"
if os.path.exists(wikikeys):
  with open(wikikeys, "r") as f: config = json.load(f)

# Wikidata is not updates in dryrun model.
dryrun = config.get("dryrun", False)
next_dryid = 1000
if dryrun: log.info("Wikibase exporter is in dryrun mode")

# Configure wikibase urls.
wikibaseurl = "https://www.wikidata.org"
if "site" in config: wikibaseurl = config["site"]
oauth_url = wikibaseurl + "/wiki/Special:OAuth"
authorize_url = wikibaseurl + "/wiki/Special:OAuth/authorize"
api_url = wikibaseurl + "/w/api.php"

# OAuth credentials.
consumer = Credentials(config["consumer_key"], config["consumer_secret"])
sessions = {}

# Initialize commons store for Wikidata export.
commons = sling.Store()
wikiconv = sling.WikiConverter(commons)
n_id = commons["id"]
n_is = commons["is"]
n_name = commons["name"]
n_description = commons["description"]
n_alias = commons["alias"]
n_media = commons["media"]
n_english = commons["/lang/en"]
n_target = commons["target"]
n_lat = commons["/w/lat"]
n_lng = commons["/w/lng"]
n_amount = commons["/w/amount"]
n_unit = commons["/w/unit"]
n_topics = commons["topics"]
n_created = commons["created"]
n_results = commons["results"]

n_item_type = commons["/w/item"]
n_string_type = commons["/w/string"]
n_text_type = commons["/w/text"]
n_xref_type = commons["/w/xref"]
n_time_type = commons["/w/time"]
n_url_type = commons["/w/url"]
n_media_type = commons["/w/media"]
n_quantity_type = commons["/w/quantity"]
n_geo_type = commons["/w/geo"]

reference_qualifiers = set([
  commons["P248"], # stated in
  commons["P854"], # reference URL
  commons["P143"], # imported from Wikimedia project
])

# Defer schema loading.
schema_loaded = False

def ensure_schema():
  global schema_loaded;
  if not schema_loaded:
    r = requests.get("https://ringgaard.com/schema/")
    r.raise_for_status()
    commons.parse(r.content)
    commons.freeze()
    schema_loaded = True
    log.info("Schema loaded for wikidata exporter")

def handle_initiate(request):
  # Get request token.
  cb = request.param("cb")
  if dryrun or cb is None: cb = "oob"

  oauth = requests_oauthlib.OAuth1Session(client_key=consumer.key,
                                          client_secret=consumer.secret,
                                          callback_uri=cb)

  oauth.fetch_request_token(oauth_url + "/initiate")

  # Remember session with token secret.
  authid = str(random.getrandbits(64))
  sessions[authid] = oauth

  # Send back redirect url for authorization.
  authurl = oauth.authorization_url(authorize_url)

  return {
    "redirect": authurl,
    "authid": authid,
  }

def handle_access(request):
  # Get token secret from auth id.
  authid = request.param("authid")
  authurl = request.param("authurl")
  oauth = sessions[authid]
  del sessions[authid]

  # Fetch user access token.
  oauth.parse_authorization_response(authurl)
  response = oauth.fetch_access_token(oauth_url + "/token")

  return {
    "key": response["oauth_token"],
    "secret": response["oauth_token_secret"],
  }

def get_credentials(request):
  return Credentials(request["Client-Key"], request["Client-Secret"])

def api_call(client, params, post=False):
  log.info("Wikibase API CALL:", params)
  oauth = requests_oauthlib.OAuth1(client_key=consumer.key,
                                   client_secret=consumer.secret,
                                   resource_owner_key=client.key,
                                   resource_owner_secret=client.secret)
  if post:
    r = requests.post(api_url, data=params, auth=oauth)
  else:
    r = requests.get(api_url, params=params, auth=oauth)

  return r.json()

qid_pat = re.compile("Q\d+")
pid_pat = re.compile("P\d+")

def is_qid(id):
  return qid_pat.fullmatch(id)

def is_pid(id):
  return pid_pat.fullmatch(id)

def get_qid(topic):
  for name, value in topic:
    if name == n_id:
      if is_qid(value): return value
    elif name == n_is:
      id = value.id
      if is_qid(id): return id

  return None

def empty_entity(entity):
  for section in ["labels", "aliases", "descriptions", "claims"]:
    if section in entity: return False
  return True

def itemtext(item):
  if type(item) is not sling.Frame: return str(item)
  name = item[n_name]
  if name is not None:
    return item[n_id] + " " + str(name)
  else:
    return item[n_id]

class WikibaseExporter:
  def __init__(self, client, topics):
    self.client = client
    self.topics = topics
    self.store = topics.store()
    self.entities = {}
    self.references = {}
    self.created = {}
    self.lang = "en"

    self.num_stubs = 0
    self.num_created = 0
    self.num_updated = 0
    self.num_unchanged = 0
    self.num_labels = 0
    self.num_descriptions = 0
    self.num_aliases = 0
    self.num_claims = 0
    self.num_qualifiers = 0
    self.num_references = 0
    self.num_skipped = 0
    self.num_errors = 0

    # Convert topics to Wikibase JSON format.
    for topic in self.topics:
      self.convert_topic(topic)

  def edit_entity(self, qid, entity):
    if dryrun:
      if "id" in entity:
        id = entity["id"]
      else:
        global next_dryid
        id = next_dryid
        next_dryid += 1
      return {"entity": {"id": "D" + str(id)}}

    command = {
      "format": "json",
      "action": "wbeditentity",
      "token": self.token,
      "data": json.dumps(entity),
    }
    if qid is None:
      command["new"] = "item"
    else:
      command["id"] = qid

    response = api_call(self.client, command, post=True)
    return response

  def publish(self):
    # Get CSFR token for editing items.
    response = api_call(self.client, {
      "format": "json",
      "action": "query",
      "meta": "tokens",
    })
    self.token = response["query"]["tokens"]["csrftoken"]

    # Publish stubs for new topics with references.
    for topic, value in self.references.items():
      entity = self.entities[topic]

      # Create stub with labels, descriptions, and aliases.
      stub = {}
      for section in ["labels", "descriptions", "aliases"]:
        if section in entity:
          stub[section] = entity[section]
          del entity[section]

      # Create stub.
      response = self.edit_entity(None, stub)
      if "error" in response:
        log.error("error creating stub:", response["error"], "stub:", stub)
        self.num_errors += 1
        continue

      # Get item id for new stub and patch value.
      itemid = response["entity"]["id"]
      item = self.store[itemid]
      topic[n_is] = item
      self.created[topic] = item
      value["value"]["id"] = itemid
      log.info("reference", topic.id, value)
      self.num_stubs += 1

    # Publish topic updates.
    for topic, entity in self.entities.items():
      if empty_entity(entity):
        self.num_unchanged += 1
        continue
      qid = get_qid(topic)
      log.info("publish", topic.id, qid, json.dumps(entity, indent=2))
      response = self.edit_entity(qid, entity)

      if "error" in response:
        log.error("error editing item:", response["error"], "entity:", entity)
        self.num_errors += 1
        continue

      if qid is None:
        itemid = response["entity"]["id"]
        item = self.store[itemid]
        topic[n_is] = item
        self.created[topic] = item
        self.num_created += 1
      else:
        self.num_updated += 1

  def convert_topic(self, topic):
    # Add entity with optional existing Wikidata item id.
    entity = {}
    self.entities[topic] = entity
    qid = get_qid(topic)

    # Fetch existing item to check for existing claims.
    current = None
    revision = None
    if qid is not None:
      current, revision = self.fetch_item(qid)

    # Add new labels, description, aliases, and claims.
    for name, value in topic:
      # Skip existing statements.
      if name == n_id or name == n_is or name == n_media:
        pass
      elif current and self.has(current, name, value):
        pass
      elif name == n_name:
        label = str(value)
        lang = self.get_language(value)
        if "labels" not in entity: entity["labels"] = {}
        entity["labels"][lang] = {"language": lang, "value": label}
        self.num_labels += 1
      elif name == n_description:
        description = str(value)
        lang = self.get_language(value)
        if "descriptions" not in entity: entity["descriptions"] = {}
        entity["descriptions"][lang] = {"language": lang, "value": description}
        self.num_descriptions += 1
      elif name == n_alias:
        alias = str(value)
        lang = self.get_language(value)
        if "aliases" not in entity: entity["aliases"] = {}
        if lang not in entity["aliases"]: entity["aliases"][lang] = []
        entity["aliases"][lang].append({"language": lang, "value": alias})
        self.num_aliases += 1
      else:
        # Only add claims for Wikidata properties.
        pid = name.id
        if not is_pid(pid): continue

        # Build main snak.
        t = name[n_target]
        v = self.store.resolve(value)
        if self.skip_value(v):
          log.info("skip unknown", itemtext(name), itemtext(v),
                   "for", itemtext(topic))
          self.num_skipped += 1
          continue

        datatype, datavalue = self.convert_value(t, v)
        if datatype is None or datavalue is None:
          log.info("skip invalid", itemtext(name), itemtext(v),
                   "for", itemtext(topic))
          self.num_skipped += 1
          continue

        snak = {
          "snaktype": "value",
          "property": pid,
          "datatype": datatype,
          "datavalue": datavalue,
        }

        # Build claim.
        claim = {
          "type": "statement",
          "rank": "normal",
          "mainsnak": snak,
        }
        self.num_claims += 1

        if v != value:
          # Add qualifiers/references.
          for qname, qvalue in value:
            if qname == n_is: continue
            pid = qname.id
            if not is_pid(pid): continue

            # Build snak for qualifier/reference.
            t = qname[n_target]
            v = self.store.resolve(qvalue)
            if self.skip_value(v):
              log.info("skip qualifier", itemtext(qname), itemtext(v),
                       "for", itemtext(topic))
              continue

            datatype, datavalue = self.convert_value(t, v)
            snak = {
              "snaktype": "value",
              "property": pid,
              "datatype": datatype,
              "datavalue": datavalue,
            }

            if qname in reference_qualifiers:
              # Add reference to claim.
              if "references" not in claim: claim["references"] = []
              claim["references"].append({
                "snaks": {
                  pid: [{
                    "snaktype": "value",
                    "property": pid,
                    "datatype": datatype,
                    "datavalue": datavalue,
                  }]
                }
              })
              self.num_references += 1
            else:
              # Add qualifier to claim.
              if "qualifiers" not in claim: claim["qualifiers"] = {}
              if pid not in claim["qualifiers"]: claim["qualifiers"][pid] = []
              claim["qualifiers"][pid].append(snak)
              self.num_qualifiers += 1

        # Add claim to entity.
        if "claims" not in entity: entity["claims"] = {}
        if pid not in entity["claims"]: entity["claims"][pid] = []
        entity["claims"][pid].append(claim)

  def convert_value(self, dt, value):
    datatype = None
    datavalue = None
    if dt == n_item_type and type(value) is sling.Frame:
      qid = get_qid(value)
      datatype = "wikibase-item"
      if qid is not None:
        datavalue = {
          "value": {
            "entity-type": "item",
            "id": qid,
          },
          "type": "wikibase-entityid"
        }
      else:
        datavalue = self.references.get(value)
        if datavalue is None:
          datavalue = {
            "value": {
              "entity-type": "item",
              "id": value.id
            },
            "type": "wikibase-entityid"
          }
          self.references[value] = datavalue
    elif dt == n_string_type:
      datatype = "string"
      datavalue = {
        "value": str(value),
        "type": "string"
      }
    elif dt == n_text_type:
      datatype = "monolingualtext"
      datavalue = {
        "value": {
          "text": str(value),
          "language": self.get_language(value),
        },
        "type": "monolingualtext"
      }
    elif dt == n_xref_type:
      datatype = "external-id"
      datavalue = {
        "value": str(value),
        "type": "string"
      }
    elif dt == n_url_type:
      datatype = "url"
      datavalue = {
        "value": str(value),
        "type": "string"
      }
    elif dt == n_time_type:
      t = sling.Date(value)
      if t.precision != 0:
        datatype = "time"
        datavalue = {
          "value": {
            "time": t.iso(),
            "precision": t.precision + 5,
            "timezone": 0,
            "before": 0,
            "after": 0,
            "calendarmodel": "http://www.wikidata.org/entity/Q1985727"
          },
          "type": "time"
        }
    elif dt == n_media_type:
      datatype = "commonsMedia"
      datavalue = {
        "value": str(value),
        "type": "string"
      }
    elif dt == n_quantity_type:
      datatype = "quantity"
      if type(value) is sling.Frame:
        amount = str(value[n_amount])
        unit = "http://www.wikidata.org/entity/" + value[n_unit].id
      else:
        amount = str(value)
        unit = "1"
      datavalue = {
        "value": {
          "amount": amount,
          "unit": unit,
        },
        "type": "quantity"
      }
    elif dt == n_geo_type:
      datatype = "globecoordinate"
      datavalue = {
        "value": {
          "latitude": value[n_lat],
          "longitude": value[n_lng],
        },
        "type": "globecoordinate"
      }

    return datatype, datavalue

  def skip_value(self, value):
    if type(value) != sling.Frame: return False
    id = value.id
    if id is None: return False
    if is_qid(id): return False
    if value in self.topics: return False
    return True

  def has(self, item, name, value):
    value = self.store.resolve(value)
    lang = self.lang
    if type(value) is sling.String:
      lang = self.get_language(value)
      value = value.text()

    qid = ""
    if type(value) is sling.Frame:
      qid = get_qid(value)

    for v in item(name):
      v = self.store.resolve(v)
      if v == value: return True
      if type(v) is sling.Frame:
        if get_qid(v) == qid: return True
      if type(v) is sling.String:
        if v.text() == value and self.get_language(v) == lang: return True

    return False

  def get_language(self, s):
    if type(s) is sling.String:
      id = s.qualifier().id
      if id.startswith("/lang/"): return id[6:]
    return self.lang

  def fetch_item(self, qid):
    # Fetch item from wikidata site.
    url = "https://www.wikidata.org/wiki/Special:EntityData/" + qid + ".json"
    r = requests.get(url)

    # Convert item to frame.
    item, revision = wikiconv.convert_wikidata(self.store, r.content)
    return item, revision

def handle_export(request):
  # Get client credentials.
  client = get_credentials(request)

  # Load schema if not already done.
  ensure_schema()

  # Get exported request.
  store = sling.Store(commons)
  export = request.frame(store)

  # Export topics to Wikidata.
  exporter = WikibaseExporter(client, export[n_topics])
  exporter.publish()

  # Return QIDs for newly created items.
  return store.frame({
    n_created: exporter.created,
    n_results: {
      "errors": exporter.num_errors,
      "created": exporter.num_created,
      "updated": exporter.num_updated,
      "unchanged": exporter.num_unchanged,
      "stubs": exporter.num_stubs,
      "labels": exporter.num_labels,
      "descriptions": exporter.num_descriptions,
      "aliases": exporter.num_aliases,
      "claims": exporter.num_claims,
      "qualifiers": exporter.num_qualifiers,
      "references": exporter.num_references,
      "skipped": exporter.num_skipped,
    }
  })

def handle(request):
  if request.path == "/export":
    return handle_export(request)
  elif request.path == "/initiate":
    return handle_initiate(request)
  elif request.path == "/access":
    return handle_access(request)
  else:
    return 501


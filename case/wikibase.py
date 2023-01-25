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

# Wikidata is not updated in dryrun mode.
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
wikiconv = sling.WikiConverter(commons, guids=True)
n_id = commons["id"]
n_is = commons["is"]
n_name = commons["name"]
n_description = commons["description"]
n_alias = commons["alias"]
n_media = commons["media"]
n_english = commons["/lang/en"]
n_guid = commons["guid"]
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

qualifier_identifiers = [
  commons["P585"], # point in time
  commons["P580"], # start time
  commons["P582"], # end time
]

reference_qualifiers = set([
  commons["P248"], # stated in
  commons["P854"], # reference URL
  commons["P143"], # imported from Wikimedia project
])

# Defer schema loading.
schema_loaded = False

# Cached QIDs for new items.
qid_cache = {}

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
      ref = value
      if type(ref) is sling.Frame: ref = ref.id
      if is_qid(ref): return ref

  return None

def same(a, b):
  if type(a) is sling.Frame:
    return a.equals(b)
  else:
    return a == b

def has_qualifiers(f):
  if type(f) is sling.Frame:
    for name, _ in f:
      if name != n_is and name != n_guid:
        return True
  return False

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

# Return obj[key1][key2].
def section(obj, key1, key2):
  l1 = obj.get(key1)
  if l1 is None:
    l1 = {}
    obj[key1] = l1
  l2 = l1.get(key2)
  if l2 is None:
    l2 = []
    l1[key2] = l2
  return l2

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
    self.num_statements = 0
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
      topic.append(n_is, itemid)
      qid_cache[topic.id] = itemid
      self.created[topic] = item
      value["value"]["id"] = itemid
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
        item.append(n_is, itemid)
        qid_cache[topic.id] = itemid
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
      elif name == n_name:
        label = str(value)
        lang = self.get_language(value)
        current_name = self.value_for_language(current, n_name, lang)
        if current_name is None:
          if "labels" not in entity: entity["labels"] = {}
          entity["labels"][lang] = {
            "language": lang,
            "value": label,
            "add": "",
          }
          self.num_labels += 1
        elif label != current_name:
          self.add_alias(entity, current, lang, label)
      elif name == n_description:
        description = str(value)
        lang = self.get_language(value)
        if self.value_for_language(current, n_description, lang) is None:
          if "descriptions" not in entity: entity["descriptions"] = {}
          entity["descriptions"][lang] = {
            "language": lang,
            "value": description,
            "add": "",
          }
          self.num_descriptions += 1
      elif name == n_alias:
        alias = str(value)
        lang = self.get_language(value)
        self.add_alias(entity, current, lang, alias)
      else:
        # Only add claims for Wikidata properties.
        if name is None: continue
        pid = name.id
        if not is_pid(pid): continue

        # Skip statements with invalid values.
        v = self.store.resolve(value)
        if self.skip_value(v):
          log.info("skip", itemtext(name), itemtext(v),
                   "for", itemtext(topic))
          self.num_skipped += 1
          continue

        # Try to find compatible existing claim.
        match = self.match(current, name, value)

        # Skip existing unqualified claim.
        if match and v == value: continue

        # Build main snak and claim.
        dt = name[n_target]
        datatype, datavalue = self.convert_value(dt, v)
        if datatype is None or datavalue is None:
          log.info("skip invalid value", itemtext(name), itemtext(v),
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
        self.num_statements += 1
        if match:
          # Set id for existing claim.
          claim["id"] = match["guid"]

          # Add existing qualifiers.
          if has_qualifiers(match):
            for qname, qvalue in match:
              if qname == n_is or qname == n_guid: continue
              dt = qname[n_target]
              v = self.store.resolve(qvalue)
              qpid = qname.id
              datatype, datavalue = self.convert_value(dt, v)
              section(claim, "qualifiers", qpid).append({
                "snaktype": "value",
                "property": qpid,
                "datatype": datatype,
                "datavalue": datavalue,
              })

        new_qualifiers = False
        if v != value:
          # Add qualifiers/references.
          for qname, qvalue in value:
            if qname is None or qname == n_is: continue
            qpid = qname.id
            if not is_pid(qpid): continue

            # Skip invalid/unknown qualifiers.
            dt = qname[n_target]
            v = self.store.resolve(qvalue)
            if self.skip_value(v):
              log.info("skip qualifier", itemtext(qname), itemtext(v),
                       "for", itemtext(topic))
              continue

            # Check for existing qualifier.
            found = False
            if match:
              for qv in match(qname):
                if same(qv, qvalue):
                  found = True
                  break
              if found: continue
            new_qualifiers = True

            # Build snak for qualifier/reference.
            datatype, datavalue = self.convert_value(dt, v)
            snak = {
              "snaktype": "value",
              "property": qpid,
              "datatype": datatype,
              "datavalue": datavalue,
            }

            if qname in reference_qualifiers:
              # Add reference to claim.
              if "references" not in claim: claim["references"] = []
              claim["references"].append({
                "snaks": {
                  qpid: [{
                    "snaktype": "value",
                    "property": qpid,
                    "datatype": datatype,
                    "datavalue": datavalue,
                  }]
                }
              })
              self.num_references += 1
            else:
              # Add qualifier to claim.
              section(claim, "qualifiers", qpid).append(snak);
              self.num_qualifiers += 1

        # Add claim to entity.
        if match is None or new_qualifiers:
          section(entity, "claims", pid).append(claim);

  def add_alias(self, entity, current, lang, alias):
    # Check for existing alias.
    if current:
      for a in current(n_alias):
        if self.get_language(a) == lang and str(a) == alias: return

    # Get/add alias section.
    aliases = entity.get("aliases")
    if aliases is None:
      aliases = {}
      entity["aliases"] = aliases

    # Get/add alias section for language and copy exising aliases.
    aliases_for_lang = aliases.get(lang)
    if aliases_for_lang is None:
      aliases_for_lang = []
      aliases[lang] = aliases_for_lang
      if current:
        for a in current(n_alias):
          if self.get_language(a) == lang:
            aliases_for_lang.append({"language": lang, "value": str(a)})

    # Add new alias.
    aliases_for_lang.append({
      "language": lang,
      "value": alias,
      "add": "",
    })
    self.num_aliases += 1

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

  def match(self, item, name, value):
    if item is None: return None
    baseval = self.store.resolve(value)
    for v in item(name):
      basev = self.store.resolve(v)
      if not same(baseval, basev): continue
      if value == baseval or not has_qualifiers(v): return v
      for ident in qualifier_identifiers:
        idval = value[ident]
        if idval is None : continue
        if idval == v[ident]: return v
    return None

  def get_language(self, s):
    if type(s) is sling.String:
      id = s.qualifier().id
      if id.startswith("/lang/"): return id[6:]
    return self.lang

  def value_for_language(self, item, prop, lang):
    if item is None: return False
    for value in item(prop):
      if self.get_language(value) == lang: return str(value)
    return None

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
  topics = export[n_topics]

  # Topic recovery.
  recovered = {}
  for topic in topics:
    has_qid = False
    for redir in topic(n_is):
      if type(redir) is sling.Frame: redir = redir.id
      if is_qid(redir):
        has_qid = True
        break
    if not has_qid and topic.id in qid_cache:
      qid = qid_cache[topic.id]
      if qid is not None:
        topic.append(n_is, qid)
        recovered[topic] = store[qid]
        log.info("recover QID", qid, "for", topic.id)

  # Export topics to Wikidata.
  exporter = WikibaseExporter(client, topics)
  exporter.publish()

  # Return recovered topics as newly created items.
  for topic, item in recovered.items():
    exporter.created[topic] = item

  # Return QIDs for newly created/recovered items.
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
      "statements": exporter.num_statements,
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


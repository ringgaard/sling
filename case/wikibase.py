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

class Credentials:
  def __init__(self, key, secret):
    self.key = key
    self.secret = secret

# Get WikiMedia application keys.
wikikeys = "local/keys/wikimedia.json"
if os.path.exists(wikikeys):
  with open(wikikeys, "r") as f: apikeys = json.load(f)

# Configure wikibase urls.
wikibaseurl = "https://www.wikidata.org"
if "site" in apikeys: wikibaseurl = apikeys["site"]
oauth_url = wikibaseurl + "/wiki/Special:OAuth"
authorize_url = wikibaseurl + "/wiki/Special:OAuth/authorize"
api_url = wikibaseurl + "/w/api.php"

# OAuth credentials.
consumer = Credentials(apikeys["consumer_key"], apikeys["consumer_secret"])
sessions = {}

# Initialize commons store for Wikidata export.
kbservice = "https://ringgaard.com"
commons = sling.Store()
commons.parse(requests.get(kbservice + "/schema/").content)
n_id = commons["id"]
n_is = commons["is"]
n_name = commons["name"]
n_description = commons["description"]
n_alias = commons["alias"]
n_instance_of = commons["P31"]
n_case_file = commons["Q108673968"]
n_english = commons["/lang/en"]
n_target = commons["target"]
n_lat = commons["/w/lat"]
n_lng = commons["/w/lng"]
n_amount = commons["/w/amount"]
n_unit = commons["/w/unit"]
n_topics = commons["topics"]
n_created = commons["created"]

n_item_type = commons["/w/item"]
n_string_type = commons["/w/string"]
n_text_type = commons["/w/text"]
n_xref_type = commons["/w/xref"]
n_time_type = commons["/w/time"]
n_url_type = commons["/w/url"]
n_media_type = commons["/w/media"]
n_quantity_type = commons["/w/quantity"]
n_geo_type = commons["/w/geo"]

commons.freeze()

def handle_initiate(request):
  # Get request token.
  cb = request.param("cb")
  if cb is None: cb = "oob"

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
  print("API CALL:", params)
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

def get_language(s):
  if type(s) == sling.String:
    id = s.qual.id
    if id.startswith("/lang/"): return id.substr(6)
  return "en"

def itemtext(item):
  name = item[n_name]
  if name is not None:
    return item[n_id] + " " + name
  else:
    return item[n_id]

class WikibaseExporter:
  def __init__(self, client, topics):
    self.client = client
    self.topics = topics
    self.store = topics.store()
    self.entities = {}
    self.deferred = {}
    self.created = {}

    for topic in self.topics:
      # Never export main case file topic.
      if topic[n_instance_of] == n_case_file: continue

      # Convert topic to Wikibase JSON format.
      self.convert_topic(topic)

    for topic, value in self.deferred.items():
      print("deferred", topic.id, value)

    for topic, entity in self.entities.items():
      print(topic.id, entity)

  def edit_entity(self, entity):
    command = {
      "format": "json",
      "action": "wbeditentity",
      "token": self.token,
      "data": json.dumps(entity),
    }
    if "id" not in entity: command["new"] = entity["type"]

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

    # Publish topic updates.
    for topic, entity in self.entities.items():
      print("publish", topic.id)
      response = self.edit_entity(entity)

      if "error" in response:
        print("error editing item:", response, "entity:", entity)
        return;

      if "id" not in entity:
        itemid = response["entity"]["id"]
        entity["id"] = itemid
        item = self.store[itemid]
        topic[n_is] = item
        self.created[topic] = item

  def convert_topic(self, topic):
    # Add entity with optional existing Wikidata item id.
    entity = {}
    qid = get_qid(topic)
    if qid is None:
      entity["type"] = "item"
    else:
      entity["id"] = qid
    self.entities[topic] = entity

    # Add labels, description, aliases, and claims.
    for name, value in topic:
      if name == n_id or name == n_is:
        pass
      elif name == n_name:
        label = str(value)
        lang = get_language(value)
        if "labels" not in entity: entity["labels"] = {}
        entity["labels"][lang] = {"language": lang, "value": label}
      elif name == n_description:
        description = str(value)
        lang = get_language(value)
        if "descriptions" not in entity: entity["descriptions"] = {}
        entity["descriptions"][lang] = {"language": lang, "value": description}
      elif name == n_alias:
        alias = str(value)
        lang = get_language(value)
        if "aliases" not in entity: entity["aliases"] = {}
        if lang not in entity["aliases"]: entity["aliases"][lang] = []
        entity["aliases"][lang].append({"language": lang, "value": alias})
      else:
        # Only add claims for Wikidata properties.
        pid = name.id
        if not is_pid(pid):
          print("skip", pid)
          continue

        # Build main snak.
        t = name[n_target]
        v = self.store.resolve(value)
        if self.skip_value(v):
          print("skip", itemtext(name), itemtext(v), "for", itemtext(topic))
          continue

        datatype, datavalue = self.convert_value(t, v)
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

        if v != value:
          # Add qualifiers.
          claim["qualifiers"] = {}
          for qname, qvalue in value:
            if qname == n_is: continue
            pid = qname.id
            if not is_pid(pid): continue

            # Build snak for qualifier.
            t = qname[n_target]
            v = self.store.resolve(qvalue)
            if self.skip_value(v):
              print("skip qualifier", itemtext(qname), itemtext(v),
                    "for", itemtext(topic))
              continue

            datatype, datavalue = self.convert_value(t, v)
            snak = {
              "snaktype": "value",
              "property": pid,
              "datatype": datatype,
              "datavalue": datavalue,
            }

            # Add qualifier to claim.
            if pid not in claim["qualifiers"]: claim["qualifiers"][pid] = []
            claim["qualifiers"][pid].append(snak)

        # Add claim to entity.
        if "claims" not in entity: entity["claims"] = {}
        if pid not in entity["claims"]: entity["claims"][pid] = []
        entity["claims"][pid].append(claim)

  def convert_value(self, type, value):
    if type is None: type = n_item_type
    if type == n_item_type:
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
        datavalue = self.deferred.get(value)
        if datavalue is None:
          datavalue = {
            "value": {
              "entity-type": "item",
              "id": value.id
            },
            "type": "wikibase-entityid"
          }
          self.deferred[value] = datavalue
    elif type == n_string_type:
      datatype = "string"
      datavalue = {
        "value": value,
        "type": "string"
      }
    elif type == n_text_type:
      datatype = "monolingualtext"
      datavalue = {
        "language": get_language(value),
        "value": str(value),
        "type": "string"
      }
    elif type == n_xref_type:
      datatype = "external-id"
      datavalue = {
        "value": value,
        "type": "string"
      }
    elif type == n_url_type:
      datatype = "url"
      datavalue = {
        "value": value,
        "type": "string"
      }
    elif type == n_time_type:
      t = sling.Date(value)
      datatype = "time"
      datavalue = {
        "value": {
          "time": t.iso(),
          "precision": t.precision + 5
        },
        "type": "time"
      }
    elif type == n_media_type:
      datatype = "commonsMedia"
      datavalue = {
        "value": value,
        "type": "string"
      }
    elif type == n_quantity_type:
      datatype = "quantity"
      datavalue = {
        "value": {
          "amount": str(value[n_amount]),
          "unit": "http://www.wikidata.org/entity/" + value[n_unit].id,
        },
        "type": "quantity"
      }
    elif type == n_geo_type:
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

def handle_export(request):
  # Get client credentials.
  client = get_credentials(request)

  # Get exported request.
  store = sling.Store(commons)
  export = request.frame(store);

  # Export topics to Wikidata.
  exporter = WikibaseExporter(client, export[n_topics])
  exporter.publish()

  # Return QIDs for newly created items.
  return store.frame({
    n_created: exporter.created,
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


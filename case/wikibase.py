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
import hashlib
import time
import random
import base64
import hmac
import urllib.parse
import requests
import requests_oauthlib
import sling.flags as flags

wikikeys = "local/keys/wikimedia.json"

# Get WikiMedia application keys.
if os.path.exists(wikikeys):
  with open(wikikeys, "r") as f: apikeys = json.load(f)
  consumer_key = apikeys["consumer_key"]
  consumer_secret = apikeys["consumer_secret"]

# Configure wikibase urls.
wikibaseurl = "https://www.wikidata.org"
if "site" in apikeys: wikibaseurl = apikeys["site"]
oauth_url = wikibaseurl + "/wiki/Special:OAuth"
authorize_url = wikibaseurl + "/wiki/Special:OAuth/authorize"
api_url = wikibaseurl + "/w/api.php"

# Remembered token secrets for OAuth authorization.
token_secrets = {}

def quote(s):
  return urllib.parse.quote_plus(s)

def nounce():
  return str(random.getrandbits(64))

def timestamp():
  return str(int(time.time()))

def hmac_sha1(data, key):
  hashed = hmac.new(key, data.encode(), hashlib.sha1)
  digest = hashed.digest()
  return base64.b64encode(digest).decode()

def sign_request(method, url, token, secret, params=None):
  parts = urllib.parse.urlparse(url)

  pairs = []
  query = urllib.parse.parse_qs(parts.query)
  for var in query.keys():
    if var != "oauth_signature":
      pairs.append(quote(var) + "=" + quote(query[var][0]))
  if params is not None:
    for var in params.keys():
      pairs.append(quote(var) + "=" + quote(params[var]))
  pairs.sort()

  base = "&".join([
    quote(method.upper()),
    quote(parts.scheme + "://" + parts.hostname + parts.path),
    quote("&".join(pairs))
  ])

  signkey = (quote(secret) + "&" + quote(token)).encode()

  return hmac_sha1(base, signkey)

def handle_authorize(request):
  # Get request token.
  cb = request.param("cb")
  if cb is None: cb = "oob"
  params = {
    "format": "json",
	  "oauth_callback": cb,
    "oauth_consumer_key":  consumer_key,
	  "oauth_version": "1.0",
    "oauth_nonce":  nounce(),
    "oauth_timestamp": timestamp(),
	  "oauth_signature_method": "HMAC-SHA1",
  }
  url = oauth_url + "/initiate?" + urllib.parse.urlencode(params)
  signature = sign_request("GET", url, "", consumer_secret)
  url += "&oauth_signature=" + quote(signature)

  response = requests.get(url).json()
  token_key = response["key"]
  token_secret = response["secret"]

  # Remember token secret.
  authid = str(nounce())
  token_secrets[authid] = token_secret

  # Send back redirect url for authorization.
  authparams = {
    "oauth_token": token_key,
    "oauth_consumer_key": consumer_key,
    "oauth_callback": cb,
  }
  authurl = authorize_url + "?" + urllib.parse.urlencode(authparams)
  return {
    "redirect": authurl,
    "consumer": consumer_key,
    "authid": authid,
  }

def handle_access(request):
  # Get token secret from auth id.
  authid = request.param("authid")
  token_secret = token_secrets[authid]
  del token_secrets[authid]

  # Fetch user access token.
  params = {
    "format": "json",
    "oauth_verifier": request.param("oauth_verifier"),
    "oauth_consumer_key":  consumer_key,
		"oauth_token": request.param("oauth_token"),
 	  "oauth_version": "1.0",
    "oauth_nonce":  nounce(),
    "oauth_timestamp": timestamp(),
	  "oauth_signature_method": "HMAC-SHA1",
  }
  url = oauth_url + "/token?" + urllib.parse.urlencode(params)
  signature = sign_request("GET", url, token_secret, consumer_secret)
  url += "&oauth_signature=" + quote(signature)
  response = requests.get(url).json()
  return response

def api_call0(method, client_key, client_secret, params):
  header_params = {
    "oauth_consumer_key":  consumer_key,
		"oauth_token": client_key,
	  "oauth_version": "1.0",
    "oauth_nonce":  nounce(),
    "oauth_timestamp": timestamp(),
	  "oauth_signature_method": "HMAC-SHA1",
  }

  signature = sign_request(method, api_url, client_secret, consumer_secret,
                           {**header_params, **params})
  header_params["oauth_signature"] = signature;

  oauthhdrs = [];
  for name, value in header_params.items():
    oauthhdrs.append(quote(name) + '="' + quote(value) + '"')
  oauthhdr = "OAuth " + ", ".join(oauthhdrs)
  print("oauthhdr", oauthhdr)
  print("params", params)

  r = requests.method(method, api_url,
    headers={"Authorization": oauthhdr},
    data=params
  )

  return r.json()

def api_call(client_key, client_secret, params, post=False):
  print("API CALL:", params)
  auth = requests_oauthlib.OAuth1(consumer_key,
               client_secret=consumer_secret,
               resource_owner_key=client_key,
               resource_owner_secret=client_secret)
  if post:
    r = requests.post(api_url, data=params, auth=auth)
  else:
    r = requests.get(api_url, params=params, auth=auth)

  return r.json()

def handle_identify(request):
  # Get client credentials.
  client_key = request["Client-Key"]
  client_secret = request["Client-Secret"]

  # Get user information.
  response = api_call(client_key, client_secret, {
    "format": "json",
		"action": "query",
		"meta": "userinfo",
  })

  return response

def handle_token(request):
  # Get client credentials.
  client_key = request["Client-Key"]
  client_secret = request["Client-Secret"]

  # Get CSFR token for editing items.
  response = api_call(client_key, client_secret, {
    "format": "json",
    "action": "query",
    "meta": "tokens",
  })

  return response

def handle_edit(request):
  # Get client credentials.
  client_key = request["Client-Key"]
  client_secret = request["Client-Secret"]
  csfr_token = request["CSFR-Token"]

  # Request new item if there is no existing QID.
  entity = request.json()
  command = {
    "format": "json",
    "action": "wbeditentity",
    "token": csfr_token,
    "data": json.dumps(entity),
  }
  if "id" not in entity: command["new"] = entity["type"]

  response = api_call(client_key, client_secret, command, post=True)
  print("response", response)
  return response

def handle(request):
  if request.path == "/edit":
    return handle_edit(request)
  elif request.path == "/authorize":
    return handle_authorize(request)
  elif request.path == "/access":
    return handle_access(request)
  elif request.path == "/identify":
    return handle_identify(request)
  elif request.path == "/token":
    return handle_token(request)
  else:
    return 501


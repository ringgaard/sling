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
import sling.flags as flags

wikikeys = "local/keys/wikimedia.json"
wikibaseurl = "https://test.wikidata.org"
oauth_url = wikibaseurl + "/wiki/Special:OAuth"
authorize_url = wikibaseurl + "/wiki/Special:OAuth/authorize"

# Get WikiMedia application keys.
if os.path.exists(wikikeys):
  with open(wikikeys, "r") as f: apikeys = json.load(f)
  consumer_key = apikeys["consumer_key"]
  consumer_secret = apikeys["consumer_secret"]

token_secrets = {}

def quote(s):
  return urllib.parse.quote_plus(s)

def nounce():
  return random.getrandbits(64)

def timestamp():
  return str(int(time.time()))

def hmac_sha1(data, key):
  hashed = hmac.new(key, data.encode(), hashlib.sha1)
  digest = hashed.digest()
  return base64.b64encode(digest).decode()

def sign_request(method, url, token, secret):
  parts = urllib.parse.urlparse(url)

  pairs = []
  query = urllib.parse.parse_qs(parts.query)
  for var in sorted(query.keys()):
    if var != "oauth_signature":
      pairs.append(var + "=" + query[var][0])

  base = "&".join([
    quote(method.upper()),
    quote(parts.scheme + "://" + parts.hostname + parts.path),
    quote("&".join(pairs))
  ])

  signkey = (quote(secret) + "&" + quote(token)).encode()

  return hmac_sha1(base, signkey)

def handle_authorize(request):
  # Get request token.
  params = {
    "format": "json",
	  "oauth_callback": "oob",
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
  cb = request.param("cb")
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

def handle(request):
  if request.path == "/authorize":
    return handle_authorize(request)
  elif request.path == "/access":
    return handle_access(request)
  else:
    return 501


# Copyright 2024 Ringgaard Research ApS
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

"""Scan Statstidende for new death notices."""

import requests
import json
import sling
import sling.flags as flags

flags.define("--db",
             default="statstidende",
             help="message database")

flags.define("--cert",
             default="oces.pem",
             help="public certificate for authentication")

flags.define("--key",
             default="oces.key",
             help="private key for certificate")

flags.define("--date",
             default=None,
             help="message publication date",
             metavar="YYYY-MM-DD")

flags.parse()

db = sling.Database(flags.arg.db)

print("Fetching messages from Statstidende...")
cert = (flags.arg.cert, flags.arg.key)
url = "https://api.statstidende.dk/v1/messages"
if flags.arg.date: url += "?publicationdate=" + flags.arg.date
r = requests.get(url, cert=cert)
r.raise_for_status()

messages = r.json();
print(len(messages), "messages")
for message in messages:
  msgid = message["messageNumber"]
  outcome = db.put(msgid, json.dumps(message), mode=sling.DBADD)
  print("Message", msgid, outcome)
  #print(json.dumps(message, indent=2))

db.close()


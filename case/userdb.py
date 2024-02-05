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

"""Server-based case store for users"""

import hashlib
import json
import os
import os.path
import sling
import sling.net
import sling.flags as flags
import sling.log as log

commons = sling.Store()

n_main = commons["main"]
n_name = commons["name"]
n_description = commons["description"]
n_created = commons["created"]
n_modified = commons["modified"]
n_shared = commons["shared"]
n_share = commons["share"]
n_publish = commons["publish"]
n_collaborate = commons["collaborate"]
n_secret = commons["secret"]
n_link = commons["link"]
commons.freeze()

users = {}

def metadata(caseid, casefile):
  main = casefile[n_main]
  meta = {
    "id": caseid,
    "name": main[n_name],
    "description": main[n_description],
    "created": casefile[n_created],
    "modified": casefile[n_modified],
    "shared": casefile[n_shared],
    "share": bool(casefile[n_share]),
    "publish": bool(casefile[n_publish]),
    "collaborate": bool(casefile[n_collaborate]),
    "secret": casefile[n_secret],
    "link": bool(casefile[n_link]),
  }
  return meta

class User:
  def __init__(self, entry):
    self.username = entry[0]
    self.credentials = entry[1]
    self.homedir = flags.arg.userdb + "/" + self.username
    self.index = None

  def load_index(self):
    # Load case index for user if not already done.
    if self.index is None:
      with open(self.homedir + "/index.json") as f:
        self.index = json.load(f)

    return self.index

  def save_index(self):
    with open(self.homedir + "/index.json", "w") as f:
      json.dump(self.index, f)

  def handle_index(self):
    # Return case directory to user.
    log.info("Case index for", self.username);
    return self.load_index()

  def handle_fetch_case(self, caseid):
    log.info("Load case #%d for %s" % (caseid, self.username));
    filename = "%s/%d.sling" % (self.homedir, caseid)
    return sling.net.HTTPFile(filename, "application/sling")

  def handle_store_case(self, caseid, data):
    # Parse case.
    store = sling.Store(commons)
    casefile = store.parse(data)
    log.info("Save case #%d for %s" % (caseid, self.username));

    # Build meta record.
    meta = metadata(caseid, casefile)

    # Write case to store.
    filename = "%s/%d.sling" % (self.homedir, caseid)
    with open(filename, "wb") as f: f.write(data)

    # Update directory index.
    self.load_index()
    self.index[str(caseid)] = meta
    self.save_index()

    # Return directory entry.
    return meta

  def handle_store_link(self, caseid, data):
    # Parse case.
    store = sling.Store(commons)
    casefile = store.parse(data)
    log.info("Link case #%d for %s" % (caseid, self.username));

    # Build meta record.
    meta = metadata(caseid, casefile)

    # Update directory index.
    self.load_index()
    self.index[str(caseid)] = meta
    self.save_index()

    # Return directory entry.
    return meta

  def handle_delete_case(self, caseid):
    log.info("Delete case #%d for %s" % (caseid, self.username));
    self.load_index()
    meta = self.index.get(str(caseid))
    if meta is None: return 404

    # Update index directory.
    del self.index[str(caseid)]
    self.save_index()

    # Remove case file unless it is a link.
    if not meta["link"]:
      filename = "%s/%d.sling" % (self.homedir, caseid)
      os.remove(filename)

    return 200

def handle(request):
  # Authenticate request.
  parts = request.path.split("/")
  if len(parts) < 2: return 400
  user = users.get(parts[1])
  if user is None: return 401
  if request["Credentials"] != user.credentials: return 401

  # Dispatch request.
  if request.method == "GET":
    if len(parts) == 2:
      return user.handle_index()
    else:
      if not parts[2].isnumeric(): return 400
      caseid = int(parts[2])
      return user.handle_fetch_case(caseid)
  elif request.method == "PUT":
    if len(parts) != 3 or not parts[2].isnumeric(): return 400
    caseid = int(parts[2])
    return user.handle_store_case(caseid, request.body)
  elif request.method == "LINK":
    if len(parts) != 3 or not parts[2].isnumeric(): return 400
    caseid = int(parts[2])
    return user.handle_store_link(caseid, request.body)
  elif request.method == "DELETE":
    if len(parts) != 3 or not parts[2].isnumeric(): return 400
    caseid = int(parts[2])
    return user.handle_delete_case(caseid)
  else:
    return 405

def init():
  passwd_fn = flags.arg.userdb + "/passwd"
  if not os.path.exists(passwd_fn): return
  f = open(passwd_fn)
  for line in f:
    entry = line.strip().split(":")
    if len(entry) < 2: continue
    user = User(entry)
    users[user.username] = user
  f.close()


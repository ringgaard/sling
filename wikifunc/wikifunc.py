# Copyright 2025 Ringgaard Research ApS
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

# WikiFunctions cache proxy

import urllib3
import json

import sling
import sling.flags as flags
import sling.log as log
import sling.net

flags.define("--port",
             help="port number for the HTTP server",
             default=8080,
             type=int,
             metavar="PORT")

# Parse command line flags.
flags.parse()

# Initialize web server.
cache = {}
pool =  urllib3.PoolManager()

app = sling.net.HTTPServer(flags.arg.port)
app.redirect("/", "/wikifunc/")
app.file("/wikifunc/wikifunc.js", "wikifunc/wikifunc.js", "text/javascript")
app.file("/wikifunc/", "wikifunc/wikifunc.html", "text/html")

@app.route("/wikifunc/item")
def handle_extract(request):
  # Get zid for item.
  zid = request.param("zid")
  if zid is None: return 500

  # Check cache.
  item = cache.get(zid)

  # Fetch WikiFunction item if not already cached.
  if item is None:
    log.info("Fetch", zid)
    url = ("https://www.wikifunctions.org/w/api.php?" +
      "action=wikilambda_fetch&format=json&origin=*&zids=" + zid)
    r = pool.request("GET", url, timeout=60)
    reply = json.loads(r.data.decode("utf-8"))
    if "error" in reply:
      error = reply["error"]
      log.error("Eror fetching", zid, ":", error)
      return 500

    item = reply[zid]["wikilambda_fetch"]
    cache[zid] = item

  return sling.net.HTTPStatic("application/json", item)

@app.route("/wikifunc/forget", method="POST")
def handle_extract(request):
  # Get zid for item.
  zid = request.param("zid")
  if zid is None: return 500

  # Remove from cache.
  if zid in cache:
    log.info("forget", zid)
    del cache[zid]

  return 200

# Run app until shutdown.
log.info("running")
app.run()
log.info("stopped")

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

"""SLING case system"""

import requests
import datetime
import socket
import time
import urllib.parse

import sling
import sling.net
import sling.util
import sling.flags as flags
import sling.log as log

import services

flags.define("--port",
             help="HTTP port",
             default=8080,
             type=int,
             metavar="PORT")

flags.define("--number",
             help="Checkpoint file for keeping track of new case numbers",
             default=None,
             metavar="FILE")

flags.define("--number_service",
             help="Endpoint for assigning new case numbers",
             default="https://ringgaard.com/case/new",
             metavar="URL")

flags.define("--casedb",
             help="database for shared cases",
             default="case",
             metavar="DB")


# Load services before parsing flags to allow services to define flags.
services.load()
flags.parse()

# Convert ISO 8601 time to unix epoch.
def iso2ts(t):
  if t is None: return None
  if t.endswith("Z"): t = t[:-1] + "+00:00"
  return int(datetime.datetime.fromisoformat(t).timestamp())

# Conver unix epoch to RFC time.
def ts2rfc(t):
  return time.strftime("%a, %d %b %Y %H:%M:%S GMT", time.gmtime(t))

# Connect to case database.
casedb = sling.Database(flags.arg.casedb, "case.py")

# Initialize HTTP server.
app = sling.net.HTTPServer(flags.arg.port)
app.redirect("/", "/c")

# Add static files.
app.static("/common", "app", internal=True)
app.static("/case/app", "case/app")
app.static("/case/plugin", "case/plugin")

# Commons store.
commons = sling.Store()
n_caseid = commons["caseid"]
n_modified = commons["modified"]
n_share = commons["share"]
commons.freeze()

# Checkpoint with next case number.
numbering = None
if flags.arg.number:
  numbering = sling.util.Checkpoint(flags.arg.number)

# Template for main page.
main_page_template = """<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name=viewport content="width=device-width, initial-scale=1">
<link rel="icon" href="/common/image/appicon.ico" type="image/x-icon" />
<script type="module" src="/case/app/main.js"></script>
</head>
<body style="display: none;">
</body>
</html>""";

@app.route("/c")
def main_page(request):
  return main_page_template

@app.route("/case/new")
def new_case(request):
  if numbering:
    # Get new case number.
    client = request["X-Forwarded-For"]
    caseid = numbering.checkpoint
    numbering.commit(caseid + 1)

    # Log new case requests with IP address.
    log.info("Assign case #%d to client %s" % (caseid, client))

    # Return the newly assigned case number.
    store = sling.Store(commons)
    return store.frame([(n_caseid, caseid)])
  elif flags.arg.number_service:
    # Redirect to remote case numbering service.
    return sling.net.HTTPRedirect(flags.arg.number_service)
  else:
    return 500

class CaseFile:
 def __init__(self, content, modified):
   self.content = content
   self.modified = modified

@sling.net.response(CaseFile)
def case_reponse(value, request, response):
  response.ct = "application/sling"
  response["Last-Modified"] = ts2rfc(value.modified)
  response.body = value.content

@app.route("/case/fetch")
def fetch_case(request):
  # Get case id.
  caseid = int(request.params()["id"][0])

  # Fetch case file from database.
  rec, ts  = casedb.get(str(caseid))
  if rec is None: return 404;

  # Return case file.
  return CaseFile(rec, ts)

@app.route("/case/share", method="POST")
def share_case(request):
  # Get shared case.
  client = request["X-Forwarded-For"]
  store = sling.Store(commons)
  casefile = request.frame(store);

  # Get case id.
  caseid = casefile[n_caseid]
  if caseid is None: return 400;

  # Get modification time.
  modified = casefile[n_modified];
  ts = iso2ts(modified)
  if ts is None: return 400;

  # Share or unshare.
  if casefile[n_share]:
    # Store case in database.
    casedb.put(str(caseid), request.body, version=ts)

    # Log case updates with IP address.
    log.info("Share case #%d version %d for client %s" % (caseid, ts, client))
  else:
    # Delete case from database.
    casedb.delete(str(caseid))

    # Log case delete with IP address.
    log.info("Unshare case #%d version %d for client %s" % (caseid, ts, client))

@app.route("/case/service")
def service_request(request):
  # Get service name.
  service = request.path
  if service.startswith("/"): service = service[1:]
  if "/" in service: service = service[:service.find("/")]

  # Let service process request.
  log.info(service, "request", request.path)
  return services.process(service, request)

non_proxy_headers = set([
  "connection",
  "content-length",
  "content-encoding",
  "content-security-policy",
  "transfer-encoding",
])

@app.route("/case/proxy")
def service_request(request):
  # Get URL.
  url = request.params()["url"][0]
  ua = request["User-Agent"]

  # Check that request is not for local network.
  addr = urllib.parse.urlsplit(url)
  ipaddr = socket.gethostbyname(addr.hostname)
  if ipaddr.startswith("10."): return 403;
  if ipaddr.startswith("192.168."): return 403;
  if ipaddr.startswith("127."): return 403;

  # Forward request.
  log.info("Proxy request for", url)
  r = requests.get(url, headers={"User-Agent": ua})

  # Relay back response.
  response = sling.net.HTTPResponse()
  response.status = r.status_code
  response.body = r.content
  response.headers = []
  for key, value in r.headers.items():
    if key.lower() in non_proxy_headers: continue
    response.headers.append((key, value))
  log.info("Return", len(response.body), "bytes")

  return response

# Initialize services.
services.init()

# Run HTTP server.
log.info("HTTP server listening on port", flags.arg.port)
app.run()
log.info("Shutdown.")


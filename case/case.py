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

import requests

import sling
import sling.net
import sling.util
import sling.flags as flags
import sling.log as log

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
             default="https://ringgaard.com/newcase",
             metavar="URL")

flags.parse()

# Initialize HTTP server.
app = sling.net.HTTPServer(flags.arg.port)

# Add static files.
app.static("/common", "app", internal=True)
app.static("/c/app", "case/app")

# Commons store.
commons = sling.Store()
commons.freeze()

# Checkpoint with next case number.
numbering = None
if flags.arg.number:
  numbering = sling.util.Checkpoint(flags.arg.number)

@app.route("/c")
def main_page(request):
  return "<html><body>Under construction</body></html>"

@app.route("/newcase")
def new_case(request):
  if numbering:
    # Get new case number.
    client = request["X-Forwarded-For"]
    caseno = numbering.checkpoint
    numbering.commit(caseno + 1)
    log.info("Assign case #%d to client %s" % (caseno, client))

    # Make new case from with the assigned case number.
    store = sling.Store(commons)
    newcase = store.frame("{=c/%d :case}" % caseno)
    return newcase
  elif flags.arg.number_service:
    # Redirect to remote case numbering service.
    return sling.net.HTTPRedirect(flags.arg.number_service)
  else:
    return 500

# Run HTTP server.
log.info("HTTP server listening on port", flags.arg.port)
app.run()
log.info("Shutdown.")


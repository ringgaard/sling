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

import sling.net
import sling.flags as flags
import sling.log as log

flags.define("--port",
             help="HTTP port",
             default=8080,
             type=int,
             metavar="PORT")

flags.parse()

# Initialize HTTP server.
app = sling.net.HTTPServer(flags.arg.port)

@app.route("/hello")
def hello(request):
  return f"<html><body>Hello world from {request.path}</body></html>"

@app.route("/secret")
def hello(request):
  return 403

@app.route("/json")
def hello(request):
  return {"name": "John", "location": ["London", "Paris", "Rome"], "age": 30}

@app.route("/license")
def readme(request):
  return sling.net.HTTPFile("LICENSE", "text/plain")

app.page("/page.html",
"""
<html>
<body>
This is a static page
</body>
</html>
""")

app.js("/app.js",
"""
square(n) {
  return n * n;
}
""")

# Add internal static files.
app.static("/common", "app", internal=True)
app.static("/lib", "app")

# Run HTTP server.
log.info("HTTP server listening on port", flags.arg.port)
app.run()

log.info("Shutdown.")


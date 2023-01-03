# Copyright 2023 Ringgaard Research ApS
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

import json
import re
import os.path
import urllib3

import sling
import sling.flags as flags
import sling.log as log
import sling.net

flags.define("--port",
             help="port number for the HTTP server",
             default=8080,
             type=int,
             metavar="PORT")

# Extractor for Wikipedia language.
class WikiExtractor:
  def __init__(self, lang):
    self.lang = lang
    self.store = sling.Store()
    self.store.load("data/wiki/languages.sling")
    self.store.load("data/wiki/calendar.sling")
    self.store.load("data/wiki/countries.sling")
    self.store.load("data/wiki/units.sling")
    templfn = "data/wiki/" + lang + "/templates.sling"
    if os.path.isfile(templfn): self.store.load(templfn)
    self.wikipedia = sling.Wikipedia(self.store, lang)
    self.store.freeze()

  def parse(self, wikitext):
    return self.wikipedia.parse(wikitext)

  def lookup(self, title):
    return self.wikipedia.lookup(title)

# Wikipedia extractors for each language.
extractors = {}

def get_extractor(lang):
  wikiex = extractors.get(lang)
  if wikiex is None:
    wikiex = WikiExtractor(lang)
    extractors[lang] = wikiex
  return wikiex

# Fetch wikitext for Wikipedia page.
pool =  urllib3.PoolManager()
def fetch_wikitext(lang, title):
  url = ("https://" + lang + ".wikipedia.org/w/api.php?" +
         "action=query&prop=revisions&format=json&formatversion=2&" +
         "rvslots=main&rvprop=content&titles=" + title.replace(' ', '_'))
  r = pool.request("GET", url, timeout=60)
  reply = json.loads(r.data.decode("utf-8"))
  page = reply["query"]["pages"][0]
  wikitext = page["revisions"][0]["slots"]["main"]["content"]
  return wikitext

# Parse command line flags.
flags.parse()

# Initialize web server.
app = sling.net.HTTPServer(flags.arg.port)
app.static("/common", "app", internal=True)
app.redirect("/", "/witex/")

# Main page.
app.page("/witex",
"""
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name=viewport content="width=device-width, initial-scale=1">
  <title>Wikipedia extractor</title>
  <link rel="icon" href="/common/image/appicon.ico" type="image/x-icon" />
  <script type="module" src="/witex/witex.js"></script>
</head>
<body style="display: none">
  <witex-app id="app">
    <md-toolbar>
      <md-toolbar-logo></md-toolbar-logo>
      <div id="title">Wikipedia extractor</div>
      <md-input
        id="wikiurl"
        type="search"
        placeholder="Enter or paste URL for Wikipedia page...">
      </md-input>
    </md-toolbar>

    <md-content>
      <wiki-ast id="ast"></wiki-ast>
    </md-content>
  </my-app>
</body>
</html>
""")

@app.route("/witex/witex.js")
def witex_js(request):
  with open("python/wiki/witex.js", "rb") as f: data = f.read()
  return sling.net.HTTPStatic("text/javascript", data)

wikipat = re.compile(r'https\:\/\/([a-z]+)\.wikipedia\.org\/wiki\/(.*)')

@app.route("/witex/extract")
def handle_extract(request):
  # Parse Wikipedia url.
  url = request.param("url")
  if url is None: return 500
  m = wikipat.fullmatch(url)
  if m is None: return 400
  lang = m.group(1)
  title = m.group(2).replace('_', ' ')

  # Fetch Wikipedia page.
  wikitext = fetch_wikitext(lang, title)

  # Get Wikipedia extractor for language.
  wikiex = get_extractor(lang)

  # Parse wikitext.
  page = wikiex.parse(wikitext)

  return {
    "lang": lang,
    "title": title,
    "qid": wikiex.lookup(title),
    "ast": page.ast(),
  }

#store = sling.Store(commons)
#doc = page.annotate(store, skip_tables=True);
#lex = sling.api.tolex(doc)

# Run app until shutdown.
log.info("running")
app.run()
log.info("stopped")


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

import json

import sling
import sling.flags as flags
import sling.log as log
import sling.net
import sling.media.photo as photolib

flags.define("--port",
             help="port number for the HTTP server",
             default=8080,
             type=int,
             metavar="PORT")

flags.define("--media_service",
             help="Media cache service",
             default=None)

flags.define("--thumb_service",
             help="Thumb nail service",
             default=None)

# Parse command line flags.
flags.parse()

# Read photo fingerprints and build reverse index.
fingerprints = {}
num_dups = 0

def add_fingerprint(photo):
  global num_dups

  fps = fingerprints.get(photo.fingerprint)
  if fps is None:
    fingerprints[photo.fingerprint] = photo
  elif type(fps) is list:
    fps.append(photo)
    num_dups += 1
  else:
    fingerprints[photo.fingerprint] = [fps, photo]
    num_dups += 1

hasher = flags.arg.hash
for url, _, data in photolib.fingerprintdb():
    fp = json.loads(data)
    if hasher not in fp: continue

    photo = photolib.Photo(fp.get("item"), url)
    photo.fingerprint = fp[hasher]
    photo.width = fp["width"]
    photo.height = fp["height"]
    add_fingerprint(photo)

    if "other" in fp:
      for otherid in fp["other"]:
        other = photolib.Photo(otherid, url)
        other.fingerprint = photo.fingerprint
        other.width = photo.width
        other.height = photo.height
        add_fingerprint(other)

log.info(len(fingerprints), "photo fingerprints,", num_dups, "dups")

# Initialize web server.
app = sling.net.HTTPServer(flags.arg.port)
app.static("/common", "app", internal=True)
app.redirect("/", "/fotomat/")

# Main page.
app.page("/fotomat",
"""
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name=viewport content="width=device-width, initial-scale=1">
  <title>Fotomat</title>
  <link rel="icon" href="/common/image/appicon.ico" type="image/x-icon" />
  <script type="module" src="/fotomat/app.js"></script>
</head>
<body style="display: none">
  <fotomat-app id="app" tabindex="0">
    <md-toolbar>
      <md-toolbar-logo></md-toolbar-logo>
      <div id="title">Fotomat</div>
      <md-input
        id="query"
        type="search"
        placeholder="Enter photo url or profile id...">
      </md-input>
      <md-icon-button id="search" icon="search"></md-icon-button>
      <md-checkbox id="onlydups" label="Only dups"></md-checkbox>
      <md-spacer></md-spacer>
      <md-icon-button id="save" icon="save"></md-icon-button>
      <md-icon-button id="copy" icon="content_copy"></md-icon-button>
      <md-icon-button id="paste" icon="content_paste"></md-icon-button>
    </md-toolbar>

    <md-content>
      <photo-profile id="photos">
     </photo-profile>
    </md-content>
  </my-app>
</body>
</html>
""")

app.file("/fotomat/app.js", "python/media/fotomat.js", "text/javascript")

@app.route("/fotomat/fetch", method="POST")
def handle_fetch(request):
  # Get query.
  query = request.body.decode()
  log.info("query", query)

  if query is None: return 500
  result = {}
  itemid = None
  if ":" in query:
    profile = photolib.Profile(None)
    profile.add_media(query)
  else:
    itemid = query
    profile = photolib.Profile(itemid)
    result["item"] = itemid
  profile.preload_fingerprints()

  captions = {}
  for media in profile.media():
    caption = profile.caption(media)
    if caption: captions[profile.url(media)] = caption

  photos = []
  result["photos"] = photos

  for photo in profile.photos().values():
    p = {
      "url": photo.url,
      "fingerprint": photo.fingerprint,
      "width": photo.width,
      "height": photo.height,
      "nsfw": photo.nsfw,
    }
    caption = captions.get(photo.url)
    if caption: p["caption"] = caption
    photos.append(p)

    fps = fingerprints.get(photo.fingerprint)
    if fps:
      if type(fps) is not list: fps = [fps]
      dups = []
      for dup in fps:
        if dup.item == itemid: continue
        dups.append({
          "item": dup.item,
          "url": dup.url,
          "fingerprint": dup.fingerprint,
          "width": dup.width,
          "height": dup.height,
        })
      if len(dups) > 0: p["dups"] = dups

  return result

@app.route("/fotomat/update", method="POST")
def handle_update(request):
  r = request.json()
  profile = photolib.Profile(None)
  profile.itemid = r["item"]
  for photo in r["photos"]:
    url = photo["url"]
    caption = photo.get("caption")
    nsfw = photo.get("nsfw")
    profile.add_photo(url, caption, None, nsfw)
  log.info("profile update", profile.itemid)
  profile.write()

@app.route("/media")
def media_request(request):
  # Dummy media service that redirects the media server or the original url.
  redir = request.path[1:]
  if flags.arg.media_service:
    redir = flags.arg.media_service + "/" + redir
  return sling.net.HTTPRedirect(redir)

@app.route("/thumb")
def thumb_request(request):
  # Dummy media service that redirects to the media server or the original url.
  redir = request.path[1:]
  if flags.arg.thumb_service:
    redir = flags.arg.thumb_service + "/" + redir
  return sling.net.HTTPRedirect(redir)

# Run app until shutdown.
log.info("running")
app.run()
log.info("stopped")

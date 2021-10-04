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

"""Search Reddit for photos."""

import sys
import sling
import sling.flags as flags
import sling.log as log
import sling.net
import sling.media.photo as photo

flags.define("--port",
             help="port number for the HTTP server",
             default=8080,
             type=int,
             metavar="PORT")

flags.define("--celebmap",
             help="list of names mapped to item ids",
             default=None,
             metavar="FILE")

# Parse command line flags.
flags.parse()
flags.arg.captionless = True

# Initialize web server.
app = sling.net.HTTPServer(flags.arg.port)
app.static("/common", "app", internal=True)
app.redirect("/", "/redreport/")

# Main page.
app.page("/redreport",
"""
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name=viewport content="width=device-width, initial-scale=1">
  <title>Reddit photo report</title>
  <link rel="icon" href="/common/image/appicon.ico" type="image/x-icon" />
  <script type="module" src="/common/lib/material.js"></script>
  <script type="module" src="/redreport/app.js"></script>
</head>
<body style="display: none">
  <photo-report-app id="app">
    <md-column-layout>
      <md-toolbar>
        <md-toolbar-logo></md-toolbar-logo>
        <md-text id="title">Reddit photo report</md-text>
        <md-spacer></md-spacer>
        <md-icon-button id="imgsearch" icon="image_search"></<md-icon-button>
      </md-toolbar>

      <md-content>
        <subreddit-list></subreddit-list>
      </md-content>
    </md-column-layout>
  </photo-report-app>
</body>
</html>
""")

app.js("/redreport/app.js",
"""
import {Component} from "/common/lib/component.js";
import {MdCard, MdDialog} from "/common/lib/material.js";
import {PhotoGallery, imageurl, use_mediadb} from "/common/lib/gallery.js";
import {reddit_thumbnail} from "/common/lib/reddit.js";

var sfw = false;
use_mediadb(false);

function current_date() {
  return new Date().toISOString().split('T')[0];
}

class PhotoReportApp extends Component {
  onconnected() {
    // Image search.
    this.bind("#imgsearch", "click", e => this.onsearch(e));

    // Get date from request; default to current date.
    let path = window.location.pathname;
    let pos = path.indexOf('/', 1);
    let date = pos == -1 ? "" : path.substr(pos + 1);
    if (date.length == 0) date = current_date();
    let qs = new URLSearchParams(window.location.search);
    if (qs.get("sfw") == "1") sfw = true;

    // Retrieve report.
    let url = `https://ringgaard.com/reddit/report/${date}.json`;
    fetch(url)
      .then(response => response.json())
      .then((report) => {
        this.find("subreddit-list").update(report);
        this.find("#title").update(`Reddit photo report for ${date}`);
      });
  }

  onsearch(e) {
    let selection = window.getSelection();
    let query = selection.toString();
    let url = `/photosearch?q="${encodeURIComponent(query)}"`;
    if (!sfw) url += "&nsfw=1";
    window.open(url, "_blank");
  }
}

Component.register(PhotoReportApp);

class PhotoDialog extends MdDialog {
  submit() {
    this.close({
      id: this.find("#id").value.trim(),
      name: this.find("#name").value.trim(),
      nsfw: this.find("#nsfw").checked,
    });
  }

  render() {
    let p = this.state;
    return `
      <md-dialog-top>Add photo</md-dialog-top>
      <div id="content">
        <md-text-field
          id="id"
          value="${p.id}"
          label="Item ID">
        </md-text-field>
        <md-text-field
          id="name"
          value="${Component.escape(p.name)}"
          label="Name">
        </md-text-field>
        <md-checkbox id="nsfw" label="NSFW" checked="${p.nsfw}">
        </md-checkbox>
      </div>
      <md-dialog-bottom>
        <button id="cancel">Cancel</button>
        <button id="submit">Add photo</button>
      </md-dialog-bottom>
    `;
  }

  static stylesheet() {
    return MdDialog.stylesheet() + `
      #content {
        display: flex;
        flex-direction: column;
        row-gap: 16px;
      }
      #id {
        width: 200px;
      }
      #name {
        width: 400px;
      }
    `;
  }
}

Component.register(PhotoDialog);

class RedditPosting extends Component {
  onconnected() {
    this.bind("img", "click", e => this.onphoto(e));
    if (this.find("#add")) {
      this.bind("#add", "click", e => this.onadd(e));
    }
  }

  onphoto(e) {
    let item = this.state;
    let posting = item.posting;

    fetch("/redreport/photos", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        url: posting.url,
        nsfw: posting.over_18,
      }),
    })
    .then((response) => {
      if (!response.ok) throw Error(response.statusText);
      return response.json();
    })
    .then((response) => {
      let gallery = new PhotoGallery();
      gallery.open(response.photos);
    });
  }

  onadd(e) {
    let item = this.state;
    let posting = item.posting;
    let name = "";
    if (!item.match) {
      let selection = window.getSelection();
      name = selection.toString();
      selection.removeAllRanges();
    }
    if (!name) name = item.query;
    let dialog = new PhotoDialog({
      name: name,
      id: item.match ? item.match : "",
      nsfw: posting.over_18,
    });
    dialog.show().then(result => {
      if (result) {
        console.log("Add image", posting.url, result);
        this.add(posting.url, result.name, result.id, result.nsfw);
      }
    });
  }

  add(url, name, id, nsfw) {
    fetch("/redreport/addmedia", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        url: url,
        name: name,
        id: id,
        nsfw: nsfw,
      }),
    })
    .then((response) => {
      if (!response.ok) throw Error(response.statusText);
      return response.json();
    })
    .then((response) => {
      let msg = "";
      if (response.images == 0) {
        msg = "(no image added)";
      } else if (response.images == 1) {
        msg = "(image added)";
      } else {
        msg = `(${response.images} images added)`;
      }

      this.find("#msg").update(msg);
    })
    .catch(error => {
      console.log("Server error", error.message, error.stack);
      this.find("#msg").update(`(Error: ${error.message})`);
    });
  }

  render() {
    let item = this.state;
    let posting = item.posting;
    let thumb = reddit_thumbnail(posting, 70);
    let permalink = `https://www.reddit.com${posting.permalink}`;

    let xpost = "";
    let xpost_list = posting.crosspost_parent_list;
    if (xpost_list && xpost_list.length == 1) {
      let xp = xpost_list[0];
      xpost = `cross-post from
        <a href="https://www.reddit.com${xp.permalink}" target="_blank"">
          ${xp.subreddit}
        </a>`;
    }

    let match = "";
    if (item.matches == 0) {
      match = `No matches for <em>${item.query}</em>`
    } else if (item.matches == 1) {
      let kburl = `https://ringgaard.com/kb/${item.match}`;
      if (!sfw) kburl += "?nsfw=1";
      match = `
         <b>${item.query}</b>:
         <a href="${kburl}" target="_blank">${item.match}</a>
       `;
    } else {
      match = `${item.matches} matches for <b>${item.query}</b>`
    }

    return `
      <img src="${thumb.url}" width="${thumb.width}" height="${thumb.height}">
      <div class="descr">
        <div class="title">${posting.title}</div>
        <div class="info">
          <span class="${posting.over_18 ? "nsfw" : "sfw"}">NSFW</span>
          <a href="${permalink}" target="_blank">${posting.id}</a>
          ${xpost}
        </div>
        <div class="match">
          <div>${match}</div>
          <md-icon-button id="add" icon="add_a_photo" outlined></md-icon-button>
          <md-text id="msg"></md-text>
        </div>
      </div>
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        font-family: verdana, arial, helvetica;
      }
      $ a {
        text-decoration: none;
      }
      $ img {
        margin: 5px;
      }
      $ .title {
        font-size: 20px;
        color: #006ABA;
      }
      $ .descr {
        margin: 5px;
        display: flex;
        flex-direction: column;
      }
      $ .descr .info {
        font-size: 13px;
        margin-top: 3px;
      }
      $ .descr .info a {
        color: #006ABA;
      }
      $ .descr .match {
        display: flex;
        align-items: center;
        font-size: 16px;
      }
      $ .nsfw {
        border-radius: 3px;
        border: 1px solid;
        font-size: 12px;
        padding: 2px 4px;
        margin: 2px;
        color: #d10023;
      }
      $ .sfw {
        display: none;
      }
    `;
  }
}

Component.register(RedditPosting);

class SubredditCard extends MdCard {
  render() {
    let sr = this.state
    let h = []

    // Render header.
    h.push(`
      <h1><a href="https://www.reddit.com/r/${sr.name}/">${sr.name}</a></h1>
      <p>${sr.matches} / ${sr.total} matched</p>
    `);

    // Render postings.
    for (let item of sr.unmatched) {
      if (sfw && item.posting.over_18) continue;
      h.push(new RedditPosting(item));
    }

    return h;
  }

  static stylesheet() {
    return super.stylesheet() + `
      $ {
        font-family: verdana, arial, helvetica;
      }
      $ h1 a {
        text-decoration: none;
        color: #006ABA;
      }
      $ h1 a:visited {
        text-decoration: none;
        color: #006ABA;
      }
      $ h1 {
        font-size: 24px;
      }
    `;
  }
}

Component.register(SubredditCard);

class SubredditList extends Component {
  render() {
    if (!this.state) return;
    let subreddits = this.state["subreddits"]
    let srnames = Object.keys(subreddits)
    srnames.sort();
    let cards = [];
    for (let name of srnames) {
      let report = subreddits[name]
      if (report.unmatched.length == 0) continue;
      report["name"] = name;
      let card = new SubredditCard(report);
      cards.push(card);
    }
    return cards;
  }
}

Component.register(SubredditList);

document.body.style = null;
""")

celebmap = {}

def add_celeb(name, id):
  if flags.arg.celebmap is None: return
  if name is None or len(name) == 0: return
  if name in celebmap: return

  f = open(flags.arg.celebmap, "a")
  f.write("%s: %s\n" % (name, id))
  f.close()
  celebmap[name] = id
  print("map", name, "to", id)

@app.route("/redreport/addmedia", method="POST")
def add_media(request):
  # Get request.
  r = request.json();
  url = r.get("url")
  name = r.get("name")
  id = r.get("id")
  nsfw = r.get("nsfw")
  print("***", id, name, url, "NSFW" if nsfw else "")
  if id is None or id == "": return 400
  if url is None or url == "": return 400

  # Add media to profile.
  profile = photo.Profile(id)
  n = profile.add_media(url, None, nsfw)
  if n > 0: profile.write()

  # Add name mapping to celeb map.
  add_celeb(name, id)

  sys.stdout.flush()
  return {"images": n}

@app.route("/redreport/photos", method="POST")
def photos(request):
  # Get request.
  r = request.json();
  url = r.get("url")
  nsfw = r.get("nsfw")
  if url is None or url == "": return 400

  # Get photos from url.
  profile = photo.Profile(None)
  profile.add_media(url, None, nsfw)

  # Return extracted photos.
  photos = []
  for media in profile.frame(photo.n_media):
    if type(media) is sling.Frame:
      photos.append({
        "url": media.resolve(),
        "nsfw": media[photo.n_has_quality] == photo.n_nsfw,
        "text": media[photo.n_legend],
      });
    else:
      photos.append({"url": media})
  return {"photos": photos}

# Run app until shutdown.
log.info("running")
app.run()
log.info("stopped")


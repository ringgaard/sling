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
app = sling.net.HTTPServer(flags.arg.port)
app.static("/common", "app", internal=True)

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
        <div id="title">Reddit photo report</div>
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
import {MdCard} from "/common/lib/material.js";
import {reddit_thumbnail} from "/common/lib/reddit.js";

function current_date() {
  return new Date().toISOString().split('T')[0];
}

class PhotoReportApp extends Component {
  onconnected() {
    // Get date from request; default to current date.
    let path = window.location.pathname;
    let date = path.substr(path.indexOf('/', 1) + 1);
    if (date.length == 0) date = current_date();

    // Retrieve report.
    let url = `https://ringgaard.com/reddit/report/${date}.json`;
    fetch(url)
      .then(response => response.json())
      .then((report) => {
        this.find("subreddit-list").update(report);
      });
  }

  static stylesheet() {
    return `
    `;
  }
}

Component.register(PhotoReportApp);

class RedditPosting extends Component {
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
        <a href="https://www.reddit.com${xp.permalink}  target="_blank"">
          ${xp.subreddit}
        </a>`;
    }

    let match = "";
    if (item.matches == 0) {
      match = `No matches for <em>${item.query}</em>`
    } else if (item.matches == 1) {
      let kburl = `https://ringgaard.com/kb/${item.match}?nsfw=1`;
      match = `
         <b>${item.query}</b>:
         <a href="${kburl}" target="_blank">${item.match}</a>
       `;
    } else {
      match = `${item.matches} matches for <b>${item.query}</b>`
    }

    return `
      <div class="photo">
        <a href="${posting.url}" target="_blank">
          <img src="${thumb.url}" width=${thumb.width} height=${thumb.height}>
        </a>
      </div>
      <div class="descr">
        <div class="title">${posting.title}</div>
        <div class="info">
          <span class="${posting.over_18 ? "nsfw" : "sfw"}">NSFW</span>
          <a href="${permalink}" target="_blank">${posting.name}</a>
          ${xpost}
        </div>
        <div class="match">
          ${match}
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
      $ .photo {
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
        margin-bottom: 5px;
      }
      $ .descr .info a {
        color: #006ABA;
      }
      $ .descr .match {
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

# Run app until shutdown.
log.info("running")
app.run()
log.info("stopped")


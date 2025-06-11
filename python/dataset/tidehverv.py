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

import requests

import sling
import sling.flags as flags
import sling.log as log
import sling.net

flags.define("--port",
             help="port number for the HTTP server",
             default=8080,
             type=int,
             metavar="PORT")

flags.define("--search",
             help="Search backend",
             default="http://vault.ringgaard.com:7575",
             metavar="URL")

# Parse command line flags.
flags.parse()

# Read Tidehverv case file.
log.info("Reading tidehverv database")
store = sling.Store()
n_id = store["id"]
n_is = store["is"]
n_name = store["name"]
n_topics = store["topics"]
n_instance_of = store["P31"]
n_has_part = store["P527"]
n_part_of = store["P361"]
n_author = store["P50"]
n_page = store["P304"]
n_published_in = store["P1433"]
n_pubdate = store["P577"]
n_volume = store["Q1238720"]
n_issue = store["Q28869365"]
n_article = store["Q191067"]
n_human = store["Q5"]
n_author_of = store["Q65970010"]
n_contains = store["Q66759300"]
n_volume_no = store["P478"]
n_url = store["P2699"]

casedb = sling.Database("vault/case")
data = casedb["1543"]
casefile = store.parse(data)
main = store["t/1543/1"]
log.info(len(casefile[n_topics]), "topics")
store.freeze()

# Initialize web server.
app = sling.net.HTTPServer(flags.arg.port)
app.static("/common", "app", internal=True)
app.redirect("/", "/tidehverv/")

# Main page.
app.page("/tidehverv/",
"""
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name=viewport content="width=device-width, initial-scale=1">
  <title>Tidehverv</title>
  <link rel="icon" href="/common/image/appicon.ico" type="image/x-icon" />
  <script type="module" src="/tidehverv/tidehverv.js"></script>
</head>
<body style="display: none">
  <tidehverv-app id="app">
    <md-toolbar>
      <md-toolbar-logo></md-toolbar-logo>
      <div id="title">Tidehverv arkiv</div>
      <md-input
        id="query"
        type="search"
        placeholder="Søg i Tidehverv...">
      </md-input>
      <md-icon-button id="search" icon="search"></md-icon-button>
      <md-spacer></md-spacer>
      <md-icon-button id="home" icon="home"></md-icon-button>
    </md-toolbar>

    <md-content>
      <tidehverv-content id="content"></tidehverv-content>
    </md-content>
  </my-app>
</body>
</html>
""")

app.js("/tidehverv/tidehverv.js",
"""
import {Component} from "/common/lib/component.js";
import {MdApp, MdCard, inform} from "/common/lib/material.js";

const icons = {
  "article": "text_snippet",
  "issue": "description",
  "volume": "menu_book",
  "author": "person",
};

class TidehvervApp extends MdApp {
  onconnected() {
    this.attach(this.onsearch, "click", "#search");
    this.attach(this.onclick, "click");
    window.onkeydown = e => {
      if (e.key === "Enter") this.onsearch();
      if (e.key === "Escape") this.onclear();
    }
    this.onhome();
  }

  async onsearch() {
    let query = this.find("#query").value().trim();
    this.style.cursor = "wait";
    try {
      let r = await fetch(`/tidehverv/search?q=${encodeURIComponent(query)}`);
      let results = await r.json();
      this.find("md-content").scrollTop = 0;
      this.find("#content").update(results);
    } catch (e) {
      inform("Error fetch article: " + e.toString());
    }
    this.style.cursor = "";
  }

  onclear() {
    this.find("#query").clear();
  }

  async onclick(e) {
    let target = e.target;
    let type = target.getAttribute("type");
    let ref = target.getAttribute("ref");
    if (type && ref) {
      if (type == "document" || type == "issue" || type == "article") {
        window.open(`/tidehverv/document?docid=${ref}`, "_blank");
      } else if (type == "author") {
        let r = await fetch("/tidehverv/author?authorid=" + ref);
        let author = await r.json();
        this.find("#content").update(author);
      } else if (type == "bio") {
        window.open(`https://ringgaard.com/kb/${ref}`, "_blank");
      } else {
        console.log(type, ref);
      }
    }
  }

  async onhome() {
    let r = await fetch("/tidehverv/overview");
    let overview = await r.json();
    this.find("#content").update(overview);
  }

  static stylesheet() {
    return `
      $ md-input {
        display: flex;
        max-width: 600px;
      }
      $ md-input input {
        font-size: 16px;
      }
      $ #title {
        padding-right: 16px;
      }
      $ h1 {
        font-size: 24px;
        font-weight: 400;
      }
    `;
  }
}

Component.register(TidehvervApp);

class TidehvervContent extends MdCard {
  render() {
    let h = [];
    let state = this.state;
    let type = state?.type;
    if (type == "overview") {
      h.push('<h1>Årgange</h1>');
      for (let v of state.volumes) {
        h.push('<div class="title">');
        h.push(`<md-icon icon="menu_book" outlined></md-icon>`);
        h.push(`<span class="link" type="volume" ref="${v.id}">`);
        h.push(Component.escape(v.name));
        h.push('</span>');
        h.push('</div>');
      }
    } else if (type == "results") {
      for (let hit of state.hits) {
        h.push('<div class="result">');

        h.push('<div class="title">');
        h.push(`<md-icon icon="${icons[hit.type]}" outlined></md-icon>`);
        h.push(`<span class="link" type="${hit.type}" ref="${hit.id}">`);
        h.push(Component.escape(hit.name));
        h.push('</span>');
        h.push('</div>');

        h.push('<div class="snippet">');
        if (hit.type == "author") {
          if (hit.articles == 1) {
            h.push('forfatter til en artikel');
          } else {
            h.push(`forfatter til ${hit.articles} artikler`);
          }
          if (hit.start && hit.end) {
            if (hit.start == hit.end) {
              h.push(` i ${hit.start}`);
            } else {
              h.push(` i perioden ${hit.start}-${hit.end}`);
            }
          }
        } else if (hit.type == "article") {
          h.push('artikel');
          if (hit.author) {
            h.push(' af ');
            if (hit.authorid) {
              h.push(`<span class="link" type="author" ref="${hit.authorid}">`);
              h.push(Component.escape(hit.author));
              h.push('</span>');
            } else {
              h.push(Component.escape(hit.author));
            }
          }
          if (hit.issue) {
            h.push(' i ');
            h.push(Component.escape(hit.issue));
          }
          if (hit.page) {
            h.push(', side ');
            h.push(hit.page);
          }
        } else if (hit.type == "issue") {
          h.push('tidsskrift');
          if (hit.articles > 1) {
            h.push(', ');
            h.push(hit.articles);
            h.push(' artikler');
          }
          if (hit.pages) {
            h.push(', side ');
            h.push(hit.pages);
          }
        } else if (hit.type == "volume") {
          h.push(hit.volume);
          h.push('. årgang, ');
          h.push(hit.issues);
          h.push(' tidsskrifter');
        }

        h.push('</div>');

        //h.push('<pre>');
        //h.push(JSON.stringify(hit, null, 2));
        //h.push('</pre>');

        h.push('</div>');
      }
    } else if (type == "author") {
      h.push('<div class="title">');
      h.push(Component.escape(state.name));
      if (state.bio) {
        h.push(`<span class="button" type="bio" ref="${state.bio}">`);
        h.push('Biografi');
        h.push('</span>');
      }
      h.push('</div>');

      h.push('<table>');
      h.push('<tr>');
      h.push('<th class="name">Artikel</th>');
      h.push('<th class="year">År</th>');
      h.push('<th class="issue">Nr.</th>');
      h.push('<th class="page">Side</th>');
      h.push('</tr>');
      for (let a of state.articles) {
        h.push(`<tr class="clickable" type="issue" ref="${a.issueid}">`);

        h.push('<td class="name">');
        h.push(`<span class="link" type="issue" ref="${a.issueid}">`);
        h.push(Component.escape(a.name));
        h.push('</span>');
        h.push('</td>');

        h.push('<td class="year">');
        h.push(Math.floor(a.date / 100));
        h.push('</td>');

        h.push('<td class="issue">');
        h.push(a.issueno);
        h.push('</td>');

        h.push('<td class="page">');
        h.push(a.page);
        h.push('</td>');

        h.push('</tr>');
      }
      h.push('</table>');
    }
    if (h.length > 0) return h.join("");
  }

  static stylesheet() {
    return `
      $ {
        padding: 16px;
      }
      $ md-icon {
        padding-right: 4px;
      }
      $ div.result {
        padding-bottom: 20px;
      }
      $ div.title {
        display: flex;
        align-items: center;
        font-family: 'Times';
        font-size: 24px;
        padding-bottom: 4px;
      }
      $ div.snippet {
        font-family: 'Arial';
        font-size: 16px;
        color: #474747;
      }
      $ span.link {
        cursor: pointer;
        color: #26119E;
      }
      $ span.link:hover {
         text-decoration: underline;
      }
      $ span.button {
        cursor: pointer;
        background: #888888;
        color: #FFFFFF;
        border-radius: 5px;
        padding: 5px;
        margin-left: 5px;
        font-size: 12px;
      }
      $ span.button:hover {
        background: #AAAAAA;
      }
      $ table {
        font-size: 18px;
        font-family: times;
      }
      $ td, th {
        padding: 6px;
      }
      $ tr.clickable {
        cursor: pointer;
      }
      $ tr.clickable:hover {
        background: #EEEEEE;
      }
      $ th.name {
        text-align: left;
      }
      $ td.name {
        text-align: left;
      }
      $ td.year {
        text-align: right;
      }
      $ td.issue {
        text-align: right;
      }
      $ td.page {
        text-align: right;
      }
    `;
  }
}

Component.register(TidehvervContent);

document.body.style = null;
""")

@app.route("/tidehverv/overview")
def handle_volumes(request):
  volumes = []
  for vol in main(n_has_part):
    volumes.append({"id": vol[n_id], "name": vol[n_name]})
  return {
    "type": "overview",
    "volumes": volumes,
  }

@app.route("/tidehverv/search")
def handle_search(request):
  q = request.param("q")
  if q is None: return 500

  r = requests.get(flags.arg.search + "/search?tag=tidehverv&q=" + q)
  r.raise_for_status()

  results = r.json()
  hits = []
  for hit in results["hits"]:
    itemid = hit["docid"]
    if itemid not in store: continue
    item = store[itemid]
    kind = item[n_instance_of]
    if kind == n_article:
      issue = item[n_published_in]
      author = item[n_author]
      authorname = None
      authorid = None
      if author:
        if type(author) is sling.Frame:
          authorname = author[n_name]
          authorid = author[n_id]
        else:
          authorname = author

      hits.append({
        "type": "article",
        "id": itemid,
        "name": item[n_name],
        "author": authorname,
        "authorid": authorid,
        "issue": issue[n_is][n_name],
        "issueid": issue[n_is][n_id],
        "page": issue[n_page],
      })
    elif kind == n_issue:
      hits.append({
        "type": "issue",
        "id": itemid,
        "name": item[n_name],
        "pages": item[n_page],
        "published": item[n_pubdate],
        "articles": len(list(item(n_contains))),
      })
    elif kind == n_volume:
      hits.append({
        "type": "volume",
        "id": itemid,
        "name": item[n_name],
        "published": item[n_pubdate],
        "volume": item[n_volume_no],
        "issues": len(list(item(n_has_part))),
      })
    elif kind == n_human:
      start = None
      end = None
      num_articles = 0
      for a in item(n_author_of):
        num_articles += 1
        issue = store.resolve(a[n_published_in])
        if issue:
          pubdate = issue[n_pubdate] // 100
          if start is None or pubdate < start: start = pubdate
          if end is None or pubdate > end: end = pubdate

      hits.append({
        "type": "author",
        "id": itemid,
        "name": item[n_name],
        "articles": num_articles,
        "start": start,
        "end": end,
      })

  return {
    "type": "results",
    "total": results["total"],
    "hits": hits,
  }

@app.route("/tidehverv/author")
def handle_author(request):
  authorid = request.param("authorid")
  if authorid is None: return 400
  if authorid not in store: return 404
  item = store[authorid]
  articles = []
  for a in item(n_author_of):
    issue = store.resolve(a[n_published_in])

    articles.append({
      "name": a[n_name],
      "issueid": issue[n_id],
      "issueno": issue[n_issue],
      "date": issue[n_pubdate],
      "page": a[n_published_in][n_page],
    })

  articles.sort(key=lambda a: a["date"])
  return {
    "type": "author",
    "name": item[n_name],
    "bio": item[n_is],
    "articles": articles,
  }

@app.route("/tidehverv/document")
def handle_document(request):
  docid = request.param("docid")
  if docid is None: return 400
  if docid not in store: return 404
  item = store[docid]
  if item[n_instance_of] == n_article:
    item = store.resolve(item[n_published_in])
  url = item[n_url].resolve()
  if url is None: return 500
  return sling.net.HTTPRedirect(url)

# Run app until shutdown.
log.info("running")
app.run()
log.info("stopped")

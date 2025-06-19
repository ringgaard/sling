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
import hashlib
import base64

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

flags.define("--userdb",
             help="User database",
             default=None,
             metavar="FILE")

# Parse command line flags.
flags.parse()

# User database.

class User:
  def __init__(self, email, salt, password):
    self.email = email
    self.salt = salt
    self.password = password

  def valid(self, password):
    m = hashlib.sha256()
    m.update(self.salt)
    m.update(password.encode())
    return m.digest() == self.password

userdb = {}
if flags.arg.userdb:
  with open(flags.arg.userdb) as f:
    for line in f:
      f = line.strip().split(":")
      userdb[f[0]] = User(f[0], bytes.fromhex(f[1]), bytes.fromhex(f[2]))

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
n_access = store["P7228"]
n_no_access = store["Q66439731"]
n_login = store["Q16572632"]

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
  <meta name="robots" content="noindex">
</head>
<body style="display: none">
  <tidehverv-app id="app">
    <md-toolbar>
      <img class="logo" src="/tidehverv/logo.svg"></img>
      <div id="title">Tidehverv arkiv</div>
      <md-input
        id="query"
        type="search"
        placeholder="Søg i Tidehverv...">
      </md-input>
      <md-icon-button id="search" icon="search" tooltip="Søg"></md-icon-button>
      <md-spacer></md-spacer>
      <md-icon-button id="home" icon="home" tooltip="Hjem"></md-icon-button>
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
import {Component, html} from "/common/lib/component.js";
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
    this.attach(this.onhome, "click", "#home");
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
      this.set_content(await r.json());
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
        this.set_content(await r.json());
      } else if (type == "volume") {
        let r = await fetch("/tidehverv/volume?volumeid=" + ref);
        this.set_content(await r.json());
      } else if (type == "bio") {
        window.open(`https://ringgaard.com/kb/${ref}`, "_blank");
      }
    }
  }

  async onhome() {
    let r = await fetch("/tidehverv/overview");
    this.set_content(await r.json());
  }

  set_content(data) {
    this.find("#content").update(data);
    this.find("md-content").scrollTop = 0;
  }

  static stylesheet() {
    return `
      $ md-toolbar {
        background-color: #ba8167;
      }
      $ .logo {
        width: 64px;
        padding: 5px;
      }
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

      @media (width <= 1024px) {
        $ #title {
          display: none;
        }
      }
    `;
  }
}

Component.register(TidehvervApp);

class TidehvervContent extends MdCard {
  render() {
    let h = html``;
    let state = this.state;
    let type = state?.type;
    if (type == "overview") {
      h.html`<div class="frontpage">`;
      h.html`<div class="banner">TIDEHVERV</div>`;
      h.html`<p>
        Arkiv for Tidehverv med ${state.volumes.length} årgange,
        ${state.issues} tidsskrifter med ${state.pages} sider og
        ${state.articles} artikler skrevet af ${state.authors} skribenter.
        </p>
        <p>Årgange:`;
      for (let v of state.volumes) {
        h.push(link("volume", v.id, v.year));
      }
      h.html`</p>
        <p>Efterlysning! Vi mangler stadig nogle få tidsskrifter, for at arkivet
        er komplet:</p>
        <ul>
          <li>Tidehverv 90. årgang nr. 9, november 2016</li>
          <li>Tidehverv 92. årgang nr. 2, februar 2018 (p. 21-36)</li>
          <li>Tidehverv 92. årgang nr. 5, juni 2018 (p. 85-100)</li>
          <li>Tidehverv 92. årgang nr. 7-8, oktober-november 2018 (p. 129-176)</li>
        </ul>
        <p>Hvis du ligger inde med en eller flere af disse numre, vil vi være
        taknemmelige, hvis du vil kontakte
        <a href="https://tidehverv.dk">tidehverv.dk</a>,
        så vi kan aftale scanning af disse. Tidsskrifter kan scannes uden at
        beskadige originalen.</p>`;
    } else if (type == "results") {
      for (let hit of state.hits) {
        h.html`
          <div class="result">
            <div class="title">
              <md-icon icon="${icons[hit.type]}" outlined></md-icon>
              ${link(hit.type, hit.id, hit.name)}
           </div>
           <div class="snippet">`;

        if (hit.type == "author") {
          if (hit.articles == 1) {
            h.html`forfatter til en artikel`;
          } else {
            h.html`forfatter til ${hit.articles} artikler`;
          }
          if (hit.start && hit.end) {
            if (hit.start == hit.end) {
              h.html` i ${hit.start}`;
            } else {
              h.html` i perioden ${hit.start}-${hit.end}`;
            }
          }
        } else if (hit.type == "article") {
          h.html`artikel`;
          if (hit.author) {
            h.html` af ${link("author", hit.authorid, hit.author)}`;
          }
          if (hit.issue) {
            h.html` i ${hit.issue}`;
          }
          if (hit.page) {
            h.html`, side ${hit.page}`;
          }
        } else if (hit.type == "issue") {
          h.html`tidsskrift`;
          if (hit.articles > 1) {
            h.html`, ${hit.articles} artikler`;
          }
          if (hit.pages) {
            h.html`, side ${hit.pages}`;
          }
        } else if (hit.type == "volume") {
          h.html`${hit.volume}. årgang, ${hit.issues} tidsskrifter`;
        }

        h.html`</div></div>`;
      }
      if (state.hits.length == 0) {
        h.html`ingen match fundet`;
      }
    } else if (type == "author") {
      h.html`<div class="title">${state.name}`;
      if (state.bio) {
        h.html`<span class="button" type="bio" ref="${state.bio}">Biografi</span>`;
      }
      h.html`</div>`;

      h.html`
        <table>
        <tr>
         <th class="name">Artikel</th>
         <th class="year">År</th>
         <th class="issue">Nr.</th>
         <th class="page">Side</th>
        </tr>`;
      for (let a of state.articles) {
        h.html`
          <tr class="hilite">
            <td class="name">${link("issue", a.issueid, a.name)}</td>
            <td class="year">${Math.floor(a.date / 100)}</td>
            <td class="issue">${a.issueno}</td>
            <td class="page">${a.page}</td>
          </tr>`;
      }
      h.html`</table>`
    } else if (type == "volume") {
      h.html`<div class="title">${state.name}</div>`;

      if (state.issues?.length > 0) {
        h.html`<div class="snippet">`;
        if (state.issues.length == 1) {
          h.html`et tidsskrift:`;
        } else {
          h.html`${state.issues.length} tidsskrifter:`;
        }
        for (let issue of state.issues) {
          h.push(link("issue", issue.issueid, issue.issueno));
        }
        h.html`</div>`;
      }

      h.html`<table>
        <tr>
          <th class="name">Artikel</th>
          <th class="name">Forfatter</th>
          <th class="issue">Nr.</th>
          <th class="page">Side</th>
        </tr>`;
      for (let a of state.articles) {
        h.html`
          <tr class="hilite">
            <td class="name">${link("issue", a.issueid, a.name)}</td>
            <td class="name">${link("author", a.authorid, a.author)}</td>
            <td class="issue">${a.issueno}</td>
            <td class="page">${a.page}</td>
          </tr>`;
      }
      h.html`</table>`;
    }
    return h;
  }

  static stylesheet() {
    return `
      $ {
        padding: 16px;
        overflow: auto;
      }
      $ md-icon {
        padding-right: 4px;
      }
      $ div.result {
        padding-bottom: 20px;
      }
      $ div.frontpage {
        font-size: 16px;
      }
      $ div.banner {
        display: flex;
        justify-content: center;
        font-family:  Times New Roman;
        font-size: 120px;
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
        color: #9D0000;
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
      $ tr.hilite {
        cursor: pointer;
      }
      $ tr.hilite:hover {
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
      $ div.notice {
        font-size: 12px;
      }

      @media (width <= 1024px) {
        $ div.banner {
          font-size: 48px;
        }
      }
    `;
  }
}

Component.register(TidehvervContent);

function link(type, ref, name) {
  if (ref) {
    return html`<span class="link" type="${type}" ref="${ref}">${name}</span> `;
  } else {
    return name;
  }
}

""")

# Statistics.
volumes = []
for vol in main(n_has_part):
  volumes.append({
    "id": vol[n_id],
    "name": vol[n_name],
    "year": vol[n_pubdate],
  })

num_issues = 0
num_articles = 0
num_authors = 0
num_pages = 0
for topic in casefile[n_topics]:
  kind = topic[n_instance_of]
  if kind == n_article:
    num_articles += 1
  elif kind == n_human:
    num_authors += 1
  elif kind == n_issue:
    num_issues += 1
    pages = topic[n_page]
    if pages is None:
      print("missing page nos", topic.id, topic.name)
    else:
      f = pages.split("-")
      if len(f) != 2:
        print("invald page nos", topic.id, topic.name)
      else:
        pages = int(f[1]) - int(f[0]) + 1
        num_pages += pages

overview = {
  "type": "overview",
  "volumes": volumes,
  "issues": num_issues,
  "articles": num_articles,
  "authors": num_authors,
  "pages": num_pages,
}

@app.route("/tidehverv/overview")
def handle_overview(request):
  return overview

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

@app.route("/tidehverv/volume")
def handle_volume(request):
  volumeid = request.param("volumeid")
  if volumeid is None: return 400
  if volumeid not in store: return 404
  item = store[volumeid]
  issues = []
  articles = []
  for issue in item(n_has_part):
    issues.append({"issueno": issue[n_issue], "issueid": issue[n_id]})
    for a in issue(n_contains):
      author = a[n_author]
      articles.append({
        "name": a[n_name],
        "author": author[n_name] if type(author) is sling.Frame else author,
        "authorid": author[n_id] if type(author) is sling.Frame else None,
        "issueid": issue[n_id],
        "issueno": issue[n_issue],
        "date": issue[n_pubdate],
        "page": a[n_published_in][n_page],
      })

  return {
    "type": "volume",
    "name": item[n_name],
    "issues": issues,
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

  access = item[n_access]
  if access == n_no_access:
    response = sling.net.HTTPResponse()
    response.error(401, "Tidsskrift er ikke tilgængeligt online")
    return response
  elif access == n_login:
    auth = request["Authorization"]
    if auth:
      if not auth.startswith("Basic "): return 500
      credentials = base64.b64decode(auth[6:]).decode().split(":")
      if len(credentials) != 2: return 500
      user = userdb.get(credentials[0].lower())
      if user is None: return 401
      if not user.valid(credentials[1]): return 401
    else:
      response = sling.net.HTTPResponse()
      response.error(401, "Kræver login")
      response["WWW-Authenticate"] = 'Basic realm="Tidehverv"'
      return response

  url = item[n_url].resolve()
  if url is None: return 500
  return sling.net.HTTPRedirect(url)

with open("app/image/olielampe.png", "rb") as f: logo_png = f.read()
with open("app/image/olielampe.svg", "rb") as f: logo_svg = f.read()

@app.route("/tidehverv/logo.png")
def handle_logo(request):
  return sling.net.HTTPStatic("image/png", logo_png)

@app.route("/tidehverv/logo.svg")
def handle_logo(request):
  return sling.net.HTTPStatic("image/svg+xml", logo_svg)

# Run app until shutdown.
log.info("running")
app.run()
log.info("stopped")

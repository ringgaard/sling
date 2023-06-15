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

# News reader.

import sling
import sling.flags as flags
import sling.log as log
import sling.net
import sling.util
import sling.crawl.news as news

flags.define("--port",
             help="port number for the HTTP server",
             default=8080,
             type=int,
             metavar="PORT")

# Parse command line flags.
flags.parse()
news.init()

# Initialize web server.
app = sling.net.HTTPServer(flags.arg.port)
app.static("/common", "app", internal=True)
app.redirect("/", "/news/")

# Initialize web text analyzer.
webanalyzer = sling.WebsiteAnalysis()

# Initialize commons store.
commons = sling.Store()
n_name = commons["name"]
n_description = commons["description"]
n_publisher = commons["P123"]
n_publication_date = commons["P577"]
n_full_work = commons["P953"]
n_media = commons["media"]
n_author_name_string = commons["P2093"]
n_creator = commons["P170"]
n_language = commons["P407"]
n_lex = commons["lex"]

commons.freeze()

# Main page.
app.page("/news",
"""
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name=viewport content="width=device-width, initial-scale=1">
  <title>News</title>
  <link rel="icon" href="/common/image/appicon.ico" type="image/x-icon" />
  <script type="module" src="/news/app.js"></script>
</head>
<body style="display: none">
  <news-app id="app">
    <md-toolbar>
      <md-toolbar-logo></md-toolbar-logo>
      <div id="title">KnolNews</div>
      <md-input
        id="query"
        type="search"
        placeholder="News URL...">
      </md-input>
    </md-toolbar>
    <md-content>
      <article-panel id="article">
      </article-panel>
    </md-content>
  </news-app
</body>
</html>
""")

app.js("/news/app.js",
"""
import {store, frame} from "/common/lib/global.js";
import {Component} from "/common/lib/component.js";
import {DocumentViewer} from "/common/lib/docviewer.js";
import {MdApp, MdCard, inform} from "/common/lib/material.js";

const n_name = frame("name");
const n_description = frame("description");
const n_media = frame("media");
const n_lex = frame("lex");

class NewsApp extends MdApp {
  onconnected() {
    window.onkeydown = e => {
      if (e.key === "Enter") this.onfetch();
      if (e.key === "Escape") this.onclear();
    }
  }

  async onfetch() {
    let url = this.find("#query").value().trim();
    this.style.cursor = "wait";
    try {
      let r = await fetch(`/news/fetch?url=${encodeURIComponent(url)}`);
      let article = await store.parse(r);
      this.find("#article").update(article);
      this.find("md-content").scrollTop = 0;
      console.log(article.text(true));
    } catch (e) {
      inform("Error fetch article: " + e.toString());
    }
    this.style.cursor = "";
  }

  onclear() {
    this.find("#query").clear();
  }

  static stylesheet() {
    return `
      $ md-input {
        display: flex;
        max-width: 600px;
      }
      $ #title {
        padding-right: 16px;
      }
    `;
  }
}

Component.register(NewsApp);

class ArticlePanel extends MdCard {
  visible() { return this.state; }

  onupdated() {
    let article = this.state;
    this.find("#title").update(article.get(n_name));
    this.find("#summary").update(article.get(n_description));
    this.find("#document").update(article.get(n_lex));
    this.find("#image").update(article.get(n_media));
  }

  render() {
    return `
        <div class="content">
          <md-text id="title"></md-text>
          <md-text id="summary"></md-text>
          <md-image id="image"></md-image>
          <document-viewer id="document"></document-viewer>
        </div>
    `;
  }
  static stylesheet() {
    return `
      $ {
        display: flex;
        justify-content: center;
      }
      $ div.content {
        max-width: 800px;
      }
      $ #title {
        display: block;
        font: bold 40px helvetica;
        padding: 12px 0px;
      }
      $ #summary {
        display: block;
        font: 1.2rem helvetica;
        padding: 12px 0px;
      }
      $ #image {
        padding: 16px 0px;
      }
      $ #image img {
        max-height: 500px;
        max-width: 100%;
      }
      $ document-viewer {
        font-size: 1.2rem;
      }
    `;
  }
}

Component.register(ArticlePanel);

document.body.style = null;
""")

def fetch_news(url):
  # Trim news url.
  trimmed_url = news.trim_url(url)

  # Try to fetch article from database.
  article = news.crawldb[trimmed_url];

  # Handle redirects.
  if article and article.startswith(b"#REDIRECT "):
    trimmed_url = article[10:]
    log.info("redirect", trimmed_url)
    article = self.crawldb[trimmed_url];

  # Fetch directly if article not in database.
  if article is None:
    try:
        article = news.retrieve_article(trimmed_url)
    except Exception as e:
      log.info("Error retrieving article:", url, e)
      article = None

  return trimmed_url, article

@app.route("/news/fetch")
def handle_fetch(request):
  # Get news url.
  url = request.param("url")
  if url is None: return 500
  print("url", url)

  # Fetch news article.
  url, article = fetch_news(url)
  if article is None: return 404

  # Analyze web page.
  webanalyzer.analyze(article)

  #fps = array.array("Q", webanalyzer.fingerprints())
  #print("fingerprints", fps)

  # Extract meta data and content from article.
  page = webanalyzer.extract(article)

  #meta = page.metadata()
  #for k, v in meta.items():
  #  print(k, ":", v)

  #for ld in page.ldjson():
  #  print("LD-JSON:", ld)

  props = page.properties()
  for k, v in props.items():
    print("prop", k, ":", v)

  print(page.text())

  # Build article frame.
  store = sling.Store(commons)
  b = sling.util.FrameBuilder(store)
  b.add(n_name, props.get("title"))
  b.add(n_description, props.get("summary"))
  b.add(n_publisher, props.get("publisher"))
  b.add(n_publication_date, props.get("published"))
  b.add(n_full_work, props.get("url", url))
  b.add(n_media, props.get("image"))
  b.add(n_author_name_string, props.get("author"))
  b.add(n_creator, props.get("creator"))
  b.add(n_language, props.get("language"))
  b.add(n_lex, page.text())

  return b.create()

# Run app until shutdown.
log.info("running")
app.run()
log.info("stopped")


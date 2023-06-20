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

import re
import json

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
      <md-spacer></md-spacer>
      <md-icon-button id="copy" icon="content_copy"></md-icon-button>
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
import {store, frame, settings} from "/common/lib/global.js";
import {Component} from "/common/lib/component.js";
import {value_text, LabelCollector} from "/common/lib/datatype.js";
import {DocumentViewer} from "/common/lib/docviewer.js";
import {MdApp, MdCard, inform} from "/common/lib/material.js";

const n_name = frame("name");
const n_description = frame("description");
const n_media = frame("media");
const n_lex = frame("lex");
const n_publisher = frame("P123");
const n_published = frame("P577");
const n_url = frame("P953");
const n_item_type = frame("/w/item");
const n_time_type = frame("/w/time");

class NewsApp extends MdApp {
  onconnected() {
    this.attach(this.oncopy, "click", "#copy");
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

  oncopy(e) {
    let article = this.find("article-panel");
    if (article) article.copy();
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
    `;
  }
}

Component.register(NewsApp);

class KbLink extends Component {
  onconnected() {
    this.attach(this.onclick, "click");
  }

  onclick(e) {
    console.log("click", this.attrs.ref);
    window.open(`${settings.kbservice}/kb/${this.attrs.ref}`, "_blank");
  }

  static stylesheet() {
    return `
      $ {
        cursor: pointer;
        color: #0b0080;
      }

      $:hover {
        cursor: pointer;
        text-decoration: underline;
      }
    `;
  }
}

Component.register(KbLink);

class ArticlePanel extends MdCard {
  visible() { return this.state; }

  copy() {
    let article = this.state;
    navigator.clipboard.writeText(article.text());
  }

  async onupdate() {
    let article = this.state;
    if (article) {
      let collector = new LabelCollector(store);
      collector.add(article);
      await collector.retrieve();
    }
  }

  onupdated() {
    let article = this.state;
    this.find("#title").update(article.get(n_name));
    this.find("#summary").update(article.get(n_description));
    this.find("#document").update(article.get(n_lex));
    this.find("#image").update(article.get(n_media));
  }

  render() {
    function sitename(url) {
      try {
        let host = new URL(url).hostname;
        if (host.startsWith("www.")) host = host.slice(4);
        return host;
      } catch (e) {
        return "???";
      }
    }

    function html(value, dt) {
      if (value === undefined) return "";
      let [text, encoded] = value_text(value, null, dt);
      let anchor = Component.escape(text);
      if (encoded && dt != n_time_type) {
        let ref = value && value.id;
        return `<kb-link ref="${ref}">${anchor}</kb-link>`;
      } else {
        return anchor;
      }
    }

    let article = this.state;
    let publisher = article.get(n_publisher);
    let published = article.get(n_published);
    let url = article.get(n_url);

    let h = new Array();
    h.push('<div class="content">');
    h.push('<md-text id="title"></md-text>');

    h.push('<div class="source">');
    if (publisher) {
      h.push(`<span id="publisher">${html(publisher, n_item_type)}</span>`);
    }
    if (url) {
      h.push(`<a id="newsurl" href="${url}" target="_blank" rel="noreferrer">`);
      h.push(sitename(url));
      h.push('</a>');
    }
    h.push('</div>');

    if (published) {
      h.push(`<div id="published">${html(published, n_time_type)}</div>`);
    }
    h.push('<md-text id="summary"></md-text>');
    h.push('<md-image id="image"></md-image>');
    h.push('<document-viewer id="document"></document-viewer>');
    h.push('</div>');
    return h.join("");
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
      $ .source {
        display: flex;
        gap: 8px;
      }
      $ #publisher {
        font-weight: bold;
      }
      $ #newsurl {
        text-decoration: none;
        cursor: pointer;
        color: green;
      }
      $ #newsurl:hover {
        text-decoration: underline;
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

def sling_date(y, m, d):
  return y * 10000 + m * 100 + d

def parse_date(s):
  if s is None: return None

  m = re.match("^(\d\d\d\d)-(\d\d)-(\d\d)", s)
  if m != None:
    return sling_date(int(m[1]), int(m[2]), int(m[3]))

  return s

checked_hostnames = set()

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
    if article: print("cached", trimmed_url)

  # Fetch directly if article not in database.
  if article is None:
    if sling.net.private(url): return 403
    try:
        article = news.retrieve_article(trimmed_url)
    except Exception as e:
      log.info("Error retrieving article:", url, e)
      article = None

  return trimmed_url, article

ldtypes = [
  "Article",
  "NewsArticle",
  "ReportageNewsArticle",
  "article",
]

def extract_jsonld(ld, props):
  if type(ld) is not dict: return;
  if ld.get("@type") not in ldtypes: return

  url = ld.get("url")
  if url and "url" not in props: props["url"] = url

  publisher = ld.get("publisher")
  if publisher is not None and "publisher" not in props:
    if type(publisher) is list: publisher = publisher[0]
    name = publisher.get("name");
    if name is not None: props["publisher"] = name

  published = ld.get("datePublished")
  if published is not None and "published" not in props:
    props["published"] = published

  headline = ld.get("headline")
  if published is not None and "title" not in props:
    props["title"] = headline

  description = ld.get("description")
  if description is not None and "summary" not in props:
    props["summary"] = description

  image = ld.get("image")
  if image is not None and "image" not in props:
    url = image.get("url");
    if url is not None: props["image"] = url

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

  # Extract meta data and content from article.
  page = webanalyzer.extract(article)

  props = page.properties()
  #for k, v in props.items():
  #  print(k, ":", v)

  for j in page.jsonld():
    ld = json.loads(j)
    print(json.dumps(ld, indent=2))
    if type(ld) is list:
      for part in ld: extract_jsonld(part, props)
    else:
      graph = ld.get("@graph")
      if graph is not None:
        for part in graph: extract_jsonld(part, props)
      else:
        extract_jsonld(ld, props)

  # Get site information.
  store = sling.Store(commons)
  url = props.get("url", url)
  sitename = news.sitename(url)
  site = news.sites.get(sitename)
  if site is not None and site.qid is not None:
    publisher = store[site.qid]
  elif site is not None and  site.name is not None:
    publisher = site.name
  else:
    publisher = props.get("publisher")

  # Get publication date.
  published = parse_date(props.get("published"))

  # Build article frame.
  b = sling.util.FrameBuilder(store)
  b.add(n_name, props.get("title"))
  b.add(n_description, props.get("summary"))
  b.add(n_publisher, publisher)
  b.add(n_publication_date, published)
  b.add(n_full_work, url)
  b.add(n_media, props.get("image"))
  b.add(n_author_name_string, props.get("author"))
  b.add(n_creator, props.get("creator"))
  b.add(n_language, props.get("language"))

  text = page.text()
  if text is not None and len(text) > 0: b.add(n_lex, text)

  return b.create()

# Run app until shutdown.
log.info("running")
app.run()
log.info("stopped")


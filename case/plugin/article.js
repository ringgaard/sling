// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic for web article.

import {Store} from "/common/lib/frame.js";
import {store, frame} from "/common/lib/global.js";

import {match_link} from "/case/app/social.js";
import {SEARCHURL} from "/case/app/plugins.js";

const n_name = frame("name");
const n_description = frame("description");
const n_instance_of = frame("P31");
const n_title = frame("P1476");
const n_publisher = frame("P123");
const n_publication_date = frame("P577");
const n_full_work = frame("P953");
const n_media = frame("media");
const n_web_page = frame("Q36774");
const n_author_name_string = frame("P2093");
const n_creator = frame("P170");
const n_language = frame("P407");
const n_video_id = frame("P1651");

const page_types = {
  "Article": frame("Q5707594"),
  "article": frame("Q5707594"),
  "Text.Article": frame("Q5707594"),
  "ReportageNewsArticle": frame("Q124922"),
  "video.other": frame("Q34508"),
};

const date_patterns = [
  { pattern: /^(\d{4})-(\d{2})-(\d{2})[T ]/, fields: "YMD"},
  { pattern: /^\w+ (\w+) (\d+) \d+:\d+:\d+ \w+ (\d+)/, fields: "mDY"},
  { pattern: /^(\w+)\. (\d+), (\d+)/, fields: "mDY"},
  { pattern: /^(\d{4})(\d{2})(\d{2})[T ]/, fields: "YMD"},
];

const months = {
  "Jan": 1,
  "Feb": 2,
  "Mar": 3,
  "Apr": 4,
  "May": 5,
  "Jun": 6,
  "Jul": 7,
  "Aug": 8,
  "Sep": 9,
  "Oct": 10,
  "Nov": 11,
  "Dec": 12,
};

function parse_date(str) {
  for (let p of date_patterns) {
    let match = str.match(p.pattern);
    if (match) {
      var y, m, d;
      for (var i = 0; i < p.fields.length; i++) {
        let value = match[i + 1];
        switch (p.fields.charAt(i)) {
          case 'Y':
            y = parseInt(value);
            break;
          case 'M':
            m = parseInt(value);
            break;
          case 'm':
            m = months[value];
            break;
          case 'D':
            d = parseInt(value);
            break;
        }
      }
      let date = new Date(y, m - 1, d);
      if (!isNaN(d)) return date;
    }
  }
  let date = new Date(str);
  if (!isNaN(d)) return date;
}

function date2sling(d) {
  return d.getFullYear() * 10000 + (d.getMonth() + 1) * 100 + d.getDate();
}

function get_meta(doc, property) {
  let e = doc.querySelector(`meta[property="${property}"]`);
  if (!e) e = doc.querySelector(`meta[name="${property}"]`);
  if (!e) e = doc.querySelector(`meta[itemprop="${property}"]`);
  if (e) return e.getAttribute("content");
}

function escape_entities(str) {
  var doc = new DOMParser().parseFromString(str, "text/html");
  return doc.documentElement.textContent;
}

function until(str, delim) {
  let pos = str.indexOf(delim);
  return pos == -1 ? str : str.substring(0, pos);
}

export default class ArticlePlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let domain = url.hostname;
    if (domain.startsWith("www.")) domain = domain.substr(4);

    if (action == SEARCHURL) {
      return {
        ref: url,
        name: domain + url.pathname,
        description: "web article from " + domain,
        context: context,
        onitem: item => this.select(item),
      };
    }
  }

  async select(item) {
    // Create new topic.
    let topic = await item.context.new_topic();
    if (!topic) return;

    // Fetch profile from twitter and populate topic.
    await this.populate(item.context, topic, item.ref);
  }

  async populate(context, topic, url) {
    // Retrieve article.
    let r = await fetch(context.service("article", {url}));
    if (!r.ok) {
      r = await fetch(context.proxy(url), {headers: {
        "XUser-Agent": navigator.userAgent,
      }});
    }
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");
    let head = doc; //doc.head;

    // Collect meta information for article.
    let article = {};

    // Get OpenGraph meta properties.
    article.type = get_meta(head, "og:type");
    article.title = get_meta(head, "og:title");
    article.summary = get_meta(head, "og:description");
    article.url = get_meta(head, "og:url");
    article.image = get_meta(head, "og:image");
    article.site = get_meta(head, "og:site_name");
    article.language = get_meta(head, "og:locale");
    article.author_name = get_meta(head, "og:author");
    article.author = get_meta(head, "article:author");
    article.publisher = get_meta(head, "article:publisher");
    article.published = get_meta(head, "article:published_time");
    if (!article.published) {
      article.published = get_meta(head, "og:article:published_time");
    }
    if (!article.published) {
      article.published = get_meta(head, "og:pubdate");
    }

    // Get Twitter card meta properties.
    if (!article.title) {
      article.title = get_meta(head, "twitter:title");
    }
    if (!article.summary) {
      article.summary = get_meta(head, "twitter:description");
    }
    if (!article.domain) {
      article.domain = get_meta(head, "twitter:domain");
    }
    if (!article.site) {
      article.site = get_meta(head, "twitter:site");
    }
    if (!article.creator) {
      article.creator = get_meta(head, "twitter:creator");
    }
    if (!article.image) {
      article.image = get_meta(head, "twitter:image:src");
    }

    if (article.summary == "Læs mere her") {
      article.summary = null; // Ekstrabladet
    }

    // Get LD-JSON descriptor data.
    let ldjsons =  head.querySelectorAll('script[type="application/ld+json"]');
    for (let ldjson of ldjsons) {
      try {
        let ld = JSON.parse(ldjson.innerText);
        console.log(typeof(ld), ld);

        // Convert using RDF service.
        let r = await fetch(context.service("rdf"), {
          method: "POST",
          headers: {
            "Content-Type": "application/ld+json",
          },
          body: ldjson.innerText,
        });
        if (!r.ok) throw `Error: ${r.statusText}`;
        let st = new Store();
        let topics = await st.parse(r);
        for (let t of topics) console.log(t.text(true));

        let parts = [ld];
        if (Array.isArray(ld)) {
          parts = ld;
        } else if (ld["@graph"]) {
          parts = ld["@graph"];
        };

        for (let p of parts) {
          let type = p["@type"];
          if (type == "WebSite") continue;
          if (type == "Organization") continue;
          if (type == "BreadcrumbList") continue;

          if (!article.type && p.pageType) {
            article.type = p.pageType;
          }
          if (!article.title && p.headline) {
            article.title = p.headline;
          }
          if (!article.summary && p.description) {
            article.summary = p.description;
          }
          if (!article.publisher && p.publisher) {
            article.publisher = p.publisher.name;
          }
          if (!article.published && p.datePublished) {
            article.published = p.datePublished;
          }
          if (!article.published && p.dateCreated) {
            article.published = p.dateCreated;
          }
          if (p.author) {
            let authors = [p.author];
            if (Array.isArray(p.author)) {
              authors = p.author;
            };
            for (let author of authors) {
              if (typeof author === 'string') {
                if (!article.author_name) {
                  article.author_name = author;
                }
              } else {
                if (!article.author_type && author.type) {
                  article.author_type = author.type;
                }
                if (!article.author_name && author.name) {
                  article.author_name = author.name;
                }
                if (!article.author_name && author.byline) {
                  article.author_name = author.byline;
                }
              }
            }
          }
          if (p.creator) {
            let creators = [p.creator];
            if (Array.isArray(p.creator)) creators = p.creator;
            for (let creator of creators) {
              if (typeof creator === 'string') {
                if (!article.creator) {
                  article.creator = creator;
                }
              }
            }
          }
          if (!article.url && p.url) {
            article.url = p.url;
          }
          if (!article.image && p.image) {
            article.image = p.image.url;
          }
        }
      } catch (error) {
        console.log("error in ld-json", error);
      }
    }

    // Get Dublin Core meta information.
    if (!article.type) {
      article.type = get_meta(head, "dc.type");
    }
    if (!article.title) {
      article.title = get_meta(head, "dc.title");
    }
    if (!article.summary) {
      article.summary = get_meta(head, "dc.description");
    }
    if (!article.publisher) {
      article.publisher = get_meta(head, "dc.publisher");
    }
    if (!article.published) {
      article.published = get_meta(head, "dc.date");
    }
    if (!article.summary) {
      article.summary = get_meta(head, "dcterms.abstract");
    }
    if (!article.published) {
      article.published = get_meta(head, "dcterms.created");
    }
    if (!article.published) {
      article.published = get_meta(head, "DC.date.issued");
    }

    // Get Sailthru meta information.
    if (!article.title) {
      article.title = get_meta(head, "sailthru.title");
    }
    if (!article.summary) {
      article.summary = get_meta(head, "sailthru.description");
    }
    if (!article.image) {
      article.image = get_meta(head, "sailthru.image.full");
    }
    if (!article.author_name) {
      article.author_name = get_meta(head, "sailthru.author");
    }

    if (!article.published) {
      article.published = get_meta(head, "sailthru.date");
    }

    // Get Google+ meta information.
    if (!article.publisher) {
      let publisher = head.querySelector('link[rel="publisher"]');
      if (publisher) {
        let href = publisher.getAttribute("href");
        if (href) article.publisher = href;
      }
    }

    // Get generic meta information.
    if (!article.type) {
      article.type = get_meta(head, "pagetype");
    }
    if (!article.title) {
      article.title = get_meta(head, "title");
    }
    if (!article.summary) {
      article.summary = get_meta(head, "description");
    }
    if (!article.author_name) {
      article.author_name = get_meta(head, "author");
    }
    if (!article.published) {
      article.published = get_meta(head, "date");
    }
    if (!article.image) {
      article.image = get_meta(head, "image");
    }

    // Get document title.
    if (!article.title) {
      let title = head.querySelector("title");
      if (title) article.title = title.innerText;
    }

    // Get canonical URL.
    let canonical = head.querySelector('link[rel="canonical"]');
    if (canonical) {
      let href = canonical.getAttribute("href");
      if (href) article.url = href;
    }
    if (!article.url) {
      article.url = url;
    }

    // Get byline from body.
    if (!article.author_name) {
      let author = doc.querySelector('div.author');
      if (author) {
        article.byline = author.innerText;
      }
    }

    // Get language from HTML declaration.
    if (!article.language) {
      let lang = doc.documentElement.lang;
      if (!lang) lang = doc.body.lang;
      if (!lang) {
        let e = head.querySelector('meta[http-equiv="Content-Language"]');
        if (e && e.content) lang = e.content;
      }
      if (lang) article.language = lang;
    }

    // Get YouTube video id.
    if (article.site == "YouTube") {
      let u = new URL(article.url);
      article.videoid = u.searchParams.get("v");
    }

    // Trim title and summary.
    if (article.title) {
      article.title = until(article.title, '|');
    }
    if (article.summary) {
      article.summary = until(escape_entities(article.summary), '|');
    }
    if (article.author_name) {
      article.author_name = escape_entities(article.author_name);
    }
    if (article.byline) {
      article.byline = escape_entities(article.byline);
    }

    // Add article information to topic.
    console.log(article);
    if (article.title) {
      topic.put(n_name, article.title);
    }
    if (article.summary) {
      topic.put(n_description, article.summary);
    }

    if (article.type) {
      let type = page_types[article.type];
      if (type) {
        topic.put(n_instance_of, type);
      } else {
        topic.put(n_instance_of, n_web_page);
      }
    } else {
      topic.put(n_instance_of, n_web_page);
    }

    if (article.url) {
      // Get news site from url domain.
      let url = new URL(article.url);
      let r = await fetch(context.service("newssite", {site: url.hostname}));
      let site = await r.json();
      if (site.siteid) {
        topic.put(n_publisher, frame(site.siteid));
      }
    }
    if (!topic.has(n_publisher) && article.site) {
      let r = await fetch(context.service("newssite", {site: article.site}));
      let site = await r.json();
      if (site.siteid) {
        topic.put(n_publisher, frame(site.siteid));
      }
    }
    if (!topic.has(n_publisher) && article.publisher) {
      let [prop, identifier] = match_link(article.publisher);
      if (prop) {
        let item = await context.idlookup(prop, identifier);
        if (item) topic.put(n_publisher, item);
      }
    }
    if (!topic.has(n_publisher) && article.publisher) {
      topic.put(n_publisher, article.publisher);
    }

    if (article.published) {
      let date = parse_date(article.published);
      if (date) {
        topic.put(n_publication_date, date2sling(date));
      } else {
        topic.put(n_publication_date, article.published);
      }
    }

    if (article.title) {
      topic.put(n_title, article.title);
    }

    if (article.author_name) {
      topic.put(n_author_name_string, article.author_name);
    } else if (article.byline) {
      topic.put(n_author_name_string, article.byline);
    }

    if (article.creator) {
      topic.put(n_creator, article.creator);
    }

    if (article.language) {
      let lang = until(until(article.language, '-'), '_').toLowerCase();
      // TODO: use wiki item instead of language frame.
      topic.put(n_language, frame("/lang/" + lang));
    }

    if (article.url) {
      topic.put(n_full_work, decodeURI(article.url));
    }

    if (article.videoid) {
      topic.put(n_video_id, article.videoid);
    }

    if (article.image) {
      topic.put(n_media, article.image);
    }
  }
};


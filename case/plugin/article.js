// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic for web article.

import {store} from "/case/app/global.js";
import {match_link} from "/case/app/social.js";

const n_name = store.lookup("name");
const n_description = store.lookup("description");
const n_instance_of = store.lookup("P31");
const n_title = store.lookup("P1476");
const n_publisher = store.lookup("P123");
const n_publication_date = store.lookup("P577");
const n_full_work = store.lookup("P953");
const n_media = store.lookup("media");
const n_web_page = store.lookup("Q36774");
const n_author_name_string = store.lookup("P2093");
const n_language = store.lookup("P407");

const page_types = {
  "Article": store.lookup("Q5707594"),
  "article": store.lookup("Q5707594"),
  "ReportageNewsArticle": store.lookup("Q124922"),
};

function date2sling(d) {
  return d.getFullYear() * 10000 + (d.getMonth() + 1) * 100 + d.getDate();
}

function get_meta(doc, property) {
  let e = doc.querySelector(`meta[property="${property}"]`);
  if (e) return e.getAttribute("content");
  e = doc.querySelector(`meta[name="${property}"]`);
  if (e) return e.getAttribute("content");
  e = doc.querySelector(`meta[itemprop="${property}"]`);
  if (e) return e.getAttribute("content");
}

function escape_entities(str) {
  var doc = new DOMParser().parseFromString(str, "text/html");
  return doc.documentElement.textContent;
}

export default class ArticlePlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let domain = url.hostname;
    if (domain.startsWith("www.")) domain = domain.substr(4);

    if (action == 1) { // SEARCHURL
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
    let topic = item.context.new_topic();
    if (!topic) return;

    // Fetch profile from twitter and populate topic.
    await this.populate(item.context, topic, item.ref);

    // Update topic list.
    await item.context.editor.update_topics();
    await item.context.editor.navigate_to(topic);
  }

  async populate(context, topic, url) {
    // Retrieve article.
    let r = await fetch(context.proxy(url), {headers: {
      "XUser-Agent": navigator.userAgent,
    }});
    let html = await r.text();


    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");
    let head = doc; //doc.head;
    console.log(doc);

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
        console.log(ld);

        if (!article.type && ld["@type"]) {
          article.type = ld["@type"];
        }
        if (!article.type && ld.pageType) {
          article.type = ld.pageType;
        }
        if (!article.title && ld.headline) {
          article.title = ld.headline;
        }
        if (!article.publisher && ld.publisher) {
          article.publisher = ld.publisher.name;
        }
        if (!article.published && ld.datePublished) {
          article.published = ld.datePublished;
        }
        if (ld.author) {
          let author = ld.author;
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
        if (!article.url && ld.url) {
          article.url = ld.url;
        }
        if (!article.image && ld.image) {
          article.image = ld.image.url;
        }
      } catch (error) {
        console.log("error in ld-json", error);
      }
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
    if (!article.title) {
      article.title = get_meta(head, "title");
    }
    if (!article.summary) {
      article.summary = get_meta(head, "description");
    }
    if (!article.author_name) {
      article.author_name = get_meta(head, "author");
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
    if (!article.url) {
      let canonical = head.querySelector('link[rel="canonical"]');
      if (canonical) {
        let href = canonical.getAttribute("href");
        if (href) article.url = href;
      }
    }
    if (!article.url) {
      article.url = url;
    }

    // Get language from HTML declaration.
    if (!article.language) {
      let lang = doc.documentElement.lang;
      if (!lang) lang = doc.body.lang;
      if (lang) article.language = lang;
    }

    // Add article information to topic.
    console.log(article);
    if (article.title) {
      topic.put(n_name, article.title);
    }
    if (article.summary) {
      topic.put(n_description, escape_entities(article.summary));
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
      console.log("site", site);
      if (site.siteid) {
        topic.put(n_publisher, store.lookup(site.siteid));
      }
    }
    if (!topic.has(n_publisher)) {
      if (article.publisher) {
        let [prop, identifier] = match_link(article.publisher);
        if (prop) {
          let item = await context.idlookup(prop, identifier);
          if (item) topic.put(n_publisher, item);
        }
      }
    }

    if (article.published) {
      let date = new Date(article.published);
      if (isNaN(date)) {
        topic.put(n_publication_date, article.published);
      } else {
        topic.put(n_publication_date, date2sling(date));
      }
    }

    if (article.title) {
      topic.put(n_title, article.title);
    }

    if (article.author_name) {
      topic.put(n_author_name_string, article.author_name);
    }

    if (article.language) {
      let sep = article.language.indexOf('-');
      if (sep == -1) sep = article.language.indexOf('_');
      if (sep != -1) article.language = article.language.substring(0, sep);
      // TODO: use wiki item instead of language frame.
      topic.put(n_language, store.lookup("/lang/" + article.language));
    }

    if (article.url) {
      topic.put(n_full_work, article.url);
    }

    if (article.image) {
      topic.put(n_media, article.image);
    }
  }
};


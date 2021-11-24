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

const page_types = {
  "article": store.lookup("Q5707594"),
};

function date2sling(d) {
  return d.getFullYear() * 10000 + (d.getMonth() + 1) * 100 + d.getDate();
}

async function lookup(context, name) {
  if (name.length == 0) return null;
  let r = await context.kblookup(name, {fullmatch: 1});
  let data = await r.json();
  if (data.matches.length > 0) {
    return store.lookup(data.matches[0].ref);
  } else {
    return name;
  }
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
    let r = await fetch(context.proxy(url));
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");
    console.log(doc);

    // Get title.
    let title = null;
    let og_title = doc.querySelector('meta[property="og:title"]');
    if (og_title) title = og_title.getAttribute("content");
    if (!title) {
      let meta_title = doc.querySelector('title');
      if (meta_title) title = meta_title.innerText;
    }
    if (title) topic.put(n_name, title);

    // Get description.
    let og_description = doc.querySelector('meta[property="og:description"]');
    if (og_description) {
      topic.put(n_description, og_description.getAttribute("content"));
    }

    // Get type.
    let og_type = doc.querySelector('meta[property="og:type"]');
    if (og_type) {
      let page_type = og_type.getAttribute("content");
      if (page_type in page_types) page_type = page_types[page_type];
      topic.put(n_instance_of, page_type);
    }
    if (title) topic.put(n_title, title);

    // Get publisher.
    let og_publisher = doc.querySelector('meta[property="article:publisher"]');
    if (og_publisher) {
      let publisher = og_publisher.getAttribute("content");
      console.log("publisher", publisher);
      let [prop, identifier] = match_link(publisher);
      if (prop) {
        let item = await lookup(context, prop.id + "/" + identifier);
        topic.put(n_publisher, item);
      }
    }

    // Publication time.
    let og_pubtime =
      doc.querySelector('meta[property="article:published_time"]');
    if (og_pubtime) {
      let date = new Date(og_pubtime.getAttribute("content"));
      topic.put(n_publication_date, date2sling(date));
    }

    // Get canonical article url.
    let canonical = url.href;
    let og_url = doc.querySelector('meta[property="og:url"]');
    if (og_url) canonical = og_url.getAttribute("content");
    topic.put(n_full_work, canonical);

    // Get image.
    let og_image = doc.querySelector('meta[property="og:image"]');
    if (og_image) {
      let href = og_image.getAttribute("content");
      topic.put(n_media, href);
    }

    // TODO: meta article:author
  }
};


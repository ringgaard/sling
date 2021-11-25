// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from Wikipedia.

import {store} from "/case/app/global.js";

const n_name = store.lookup("name");
const n_is = store.lookup("is");

export default class WikipeidaPlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let lang = url.hostname.substr(0, url.hostname.indexOf('.'));
    let name = decodeURIComponent(url.pathname.substr(6).replace(/_/g, ' '));

    if (action == 1) { // SEARCHURL
      return {
        ref: url,
        name: name,
        lang: lang,
        description: "Wikipedia article",
        context: context,
        onitem: item => this.select(item),
      };
    } else if (action == 3) { // PASTEURL
      await this.populate(context, context.topic, name, lang);
      return true;
    }
  }

  async select(item) {
    // Create new topic.
    let topic = item.context.new_topic();
    if (!topic) return;

    // Add Wikipedia information to topic.
    await this.populate(item.context, topic, item.name, item.lang);

    // Update topic list.
    await item.context.editor.update_topics();
    await item.context.editor.navigate_to(topic);
  }

  async populate(context, topic, name, lang) {
    // Get Wikidata id for Wikipedia page.
    let url = `https://${lang}.wikipedia.org/w/api.php?action=query&` +
      "format=json&prop=pageprops&ppprop=wikibase_item&redirects=1&" +
      `titles=${encodeURIComponent(name)}`;
    let r = await fetch(context.proxy(url));
    let reply = await r.json();

    // Get page information.
    let pages = reply.query.pages;
    for (let pageid in pages) {
      let page = pages[pageid];
      let qid = page.pageprops.wikibase_item;
      let title = page.title;
      topic.put(n_name, title);
      topic.put(n_is, store.lookup(qid));
    }
  }
};


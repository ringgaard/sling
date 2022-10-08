// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from Wikidata item.

import {store} from "/case/app/global.js";

export default class WikidataPlugin {
  process(action, query, context) {
    let url = new URL(query);
    let qid = url.pathname.substring(6);
    if (!qid) return;

    return {
      ref: qid,
      name: qid,
      description: "Wikidata item",
      context: context,
      onitem: item => this.select(item),
    };
  }

  async select(item) {
    // Retrieve profile from wikidata service.
    let r = await fetch(item.context.service("wikidata", {qid: item.ref}));
    let data = await store.parse(r)

    // Create new topic.
    let topic = await item.context.new_topic();
    if (!topic) return;

    // Add properties from Wikidata item.
    for (let [name, value] of data) {
      topic.add(name, value)
    }

    item.context.updated(topic);
  }
};


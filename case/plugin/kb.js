// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding links to the knowledge base.

import {store} from "/case/app/global.js";
import {PASTEURL} from "/case/app/plugins.js";

const n_is = store.lookup("is");

export default class KBPlugin {
  async process(action, query, context) {
    if (action == PASTEURL) {
      let url = new URL(query);
      let id = decodeURIComponent(url.pathname.substr(4));

      let topic = context.topic;
      topic.put(n_is, store.lookup(id));
      context.updated(topic);
      return true;
    }
  }
};


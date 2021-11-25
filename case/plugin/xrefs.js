// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding cross-reference links to topics.

import {store, settings} from "/case/app/global.js";
import {match_link} from "/case/app/social.js";

const n_is = store.lookup("is");

export default class XrefPlugin {
  async process(action, url, context) {
    let [prop, identifier] = match_link(url);
    if (prop) {
      // Add xref property to topic.
      context.topic.put(prop, identifier);

      // Check if item with xref is already known.
      let item = await context.idlookup(prop, identifier);
      if (item) context.topic.put(n_is, item);
      return true;
    } else {
      return false;
    }
  }
};


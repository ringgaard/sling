// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding cross-reference links to topics.

import {store, frame, settings} from "/common/lib/global.js";

import {match_link} from "/case/app/social.js";
import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";

const n_is = frame("is");
const n_name = frame("name");

export default class XrefPlugin {
  async process(action, url, context) {
    let [prop, identifier] = match_link(url);
    if (!prop) return false;

    if (action == SEARCHURL) {
      return {
        ref: url,
        name: identifier,
        description: prop.get(n_name) + " stub",
        context: context,
        onitem: item => this.select(item),
      };
    } else if (action == PASTEURL) {
      // Add xref property to topic.
      context.topic.put(prop, identifier);

      // Check if item with xref is already known.
      let kbitem = await context.idlookup(prop, identifier);
      if (kbitem) {
        context.topic.put(n_is, kbitem.id);
      }
      context.updated(context.topic);
      return true;
    }
  }

  async select(item) {
    // Create new topic.
    let topic = await item.context.new_topic();
    if (!topic) return;

    // Create stub topic for identifier.
    let [prop, identifier] = match_link(item.ref);
    topic.put(n_name, identifier);

    // Add xref property to topic.
    topic.put(prop, identifier);

    // Check if item with xref is already known.
    let kbitem = await item.context.idlookup(prop, identifier);
    if (kbitem) {
      topic.put(n_is, kbitem.id);
    }
  }
};


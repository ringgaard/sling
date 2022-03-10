// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from VIAF.

import {store} from "/case/app/global.js";
import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";

export default class VIAFPlugin {
  async process(action, query, context) {
    let url = new URL(query);

    let viafid = url.pathname.substring(6);
    if (viafid.endsWith("/")) viafid = viafid.slice(0, -1);

    if (action == SEARCHURL) {
      return {
        ref: viafid,
        name: "VIAF cluster " + viafid,
        description: "VIAF topic",
        context: context,
        onitem: item => this.select(item),
      };
    } else if (action == PASTEURL) {
      await this.populate(context, context.topic, viafid);
      return true;
    }
  }

  async select(item) {
    // Create new topic.
    let topic = item.context.new_topic();
    if (!topic) return;

    // Add VIAF information to topic.
    await this.populate(item.context, topic, item.ref);
  }

  async populate(context, topic, viafid) {
    // Retrieve profile from VIAF service.
    let r = await fetch(context.service("viaf", {id: viafid}));
    let profile = await store.parse(r)

    // Add VIAF information to topic.
    for (let [name, value] of profile) {
      topic.put(name, value)
    }

    context.updated(topic);
  }
};


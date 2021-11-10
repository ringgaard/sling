// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from Twitter profile.

import {store, settings} from "/case/app/global.js";

export default class TwitterPlugin {
  process(action, query, context) {
    let url = new URL(query);
    let username = url.pathname.substring(1);
    console.log("twitter search for", username);
    return {
      ref: username,
      name: username,
      description: "Twitter user",
      onitem: item => this.select(item),
    };
  }

  async select(item) {
    console.log("twitter select", item);

    // Retrieve profile from twitter service.
    let qs = `user=${encodeURIComponent(item.ref)}`;
    let r = await fetch("/case/service/twitter?" + qs);
    let profile = await r.json();

    console.log("twitter profile", profile);
  }
};


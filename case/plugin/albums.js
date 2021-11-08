// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding photo albums to topics.

import {store, settings} from "/case/app/global.js";

const n_is = store.lookup("is");
const n_media = store.lookup("media");
const n_has_quality = store.lookup("P1552");
const n_not_safe_for_work = store.lookup("Q2716583");

export default class AlbumPlugin {
  async process(url, context) {
    let topic = context.topic;
    if (!topic) return false;
    console.log(`Add album ${url} to topic ${context.topic.id}`);

    let r = await fetch(`/case/service/albums?url=${encodeURIComponent(url)}`);
    let profile = await store.parse(r);
    for (let [name, value] of profile) {
      topic.add(name, value);
    }
    return true;
  }
};


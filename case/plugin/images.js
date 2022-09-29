// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding images to topics.

import {store, settings} from "/case/app/global.js";

const n_media = store.lookup("media");

function photourl(url) {
  if (url.startsWith('!')) return url.slice(1);
  return url;
}

export default class ImagePlugin {
  process(action, url, context) {
    if (!context.topic) return false;

    // Check for duplicate.
    for (let media of context.topic.all(n_media)) {
      if (photourl(store.resolve(media)) == url) {
        console.log("skip duplicate", url);
        return false;
      }
    }

    console.log(`add image ${url} to topic ${context.topic.id}`);
    context.topic.add(n_media, url);
    context.updated(context.topic);
    return true;
  }
};


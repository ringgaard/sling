// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Image de-scaling.

import {store, frame, settings} from "/common/lib/global.js";

const n_media = frame("media");

export default class DescalePlugin {
  process(action, url, context) {
    if (!context.topic) return false;
    let u = new URL(url);
    let original = u.searchParams.get("url");

    if (original) {
      // Check for duplicate.
      for (let media of context.topic.all(n_media)) {
        if (store.resolve(media) == original) {
          console.log("skip duplicate", original);
          return false;
        }
      }

      context.topic.add(n_media, original);
      context.updated(context.topic);
      return true;
    }
  }
};


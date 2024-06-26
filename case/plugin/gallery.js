// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding images from gallery.

import {store, frame, settings} from "/common/lib/global.js";

const n_is = frame("is");
const n_media = frame("media");

function photourl(url) {
  if (url.startsWith('!')) return url.slice(1);
  return url;
}

export default class GalleryPlugin {
  process(action, url, context) {
    let topic = context.topic;
    if (!topic) return false;

    // Get existing photos to remove duplicates.
    let photos = new Set();
    for (let media of topic.all(n_media)) {
      photos.add(photourl(store.resolve(media)));
    }

    // Get media urls from gallery: url.
    let num_added = 0;
    let gallery = url.slice(8).split(" ");
    for (let url of gallery) {
      url = url.trim();
      if (url.length < 8) {
        console.log("skip invalid image", url);
        continue;
      }
      if (photos.has(photourl(url))) {
        console.log("skip exising image", url);
        continue;
      }
      photos.add(url);
      topic.add(n_media, url);

      num_added++;
    }

    console.log(`${num_added} images added`);
    context.updated(topic);
    return num_added > 0;
  }
};


// Copyright 2023 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for ImageVenue thumbnails.

import {store, frame} from "/common/lib/global.js";
import {MD5} from "/common/lib/hash.js";

const n_media = frame("media");

export default class ImageVenuePlugin {
  process(action, url, context) {
    if (!context.topic) return false;

    let base = url.slice(url.lastIndexOf("/") + 1);
    let hires = base.replace("_t", "_o");
    let d = MD5(hires);
    let path = `${d[0]}${d[1]}/${d[2]}${d[3]}/${d[4]}${d[5]}`
    let photo = "https://cdn-images.imagevenue.com/" +
                path + "/" + hires;
    context.topic.add(n_media, "!" + photo);
    context.updated(context.topic);
    return true;
  }
};


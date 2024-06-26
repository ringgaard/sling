// Copyright 2023 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding hires images from thumbnails.

import {store, frame} from "/common/lib/global.js";
import {MD5} from "/common/lib/hash.js";

const n_media = frame("media");

export default class ThumbsPlugin {
  process(action, url, context) {
    if (!context.topic) return false;
    let photo;
    if (url.startsWith("https://cdn-thumbs.imagevenue.com")) {
      let base = url.slice(url.lastIndexOf("/") + 1);
      let hires = base.replace("_t", "_o");
      let d = MD5(hires);
      let path = `${d[0]}${d[1]}/${d[2]}${d[3]}/${d[4]}${d[5]}`
      photo = `!https://cdn-images.imagevenue.com/${path}/${hires}`;
    } else if (url.match(/^https:\/\/thumbs\d+.imagebam.com/)) {
      let m = url.match(
        /https\:\/\/thumbs(\d+)\.imagebam\.com\/\w+\/\w+\/\w+\/(.*)/);
      let hostno = m[1];
      let base = m[2];
      let hires = base.replace("_t", "_o");
      let d = MD5(hires);
      let path = `${d[0]}${d[1]}/${d[2]}${d[3]}/${d[4]}${d[5]}`
      photo = `!https://images${hostno}.imagebam.com/${path}/${hires}`;
    } else if (url.match(/^https:\/\/thumbs\d+.imgbox.com/)) {
      photo = "!" + url.replace("thumbs", "images").replace("_t", "_o");
    } else if (url.match(/^https\:\/\/t(\d+)\.pixhost\.to\//)) {
      let m = url.match(/^https\:\/\/t(\d+)\.pixhost\.to\/thumbs\/(.*)/);
      photo = `!https://img${m[1]}.pixhost.to/images/${m[2]}`;
    } else if (url.match(/^https\:\/\/pbs\.twimg\.com\/media\//)) {
      let m = url.match(
        /^(https\:\/\/pbs\.twimg\.com\/media\/[A-Za-z0-9_-]+)\?format=jpg/);
      if (m) photo = m[1] + ".jpg";
    }

    if (photo) {
      context.topic.add(n_media, photo);
      context.updated(context.topic);
      return true;
    }
  }
};


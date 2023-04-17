// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding images from instagram.

import {frame} from "/common/lib/global.js";

import {Drive} from "/case/app/drive.js";

const n_media = frame("media");

export default class InstagramPlugin {
  async process(action, url, context) {
    if (!context.topic) return false;

    // Get photo url.
    let m = new URL(url).pathname.match(/^\/p\/([A-Za-z0-9_\-]+)\//);
    if (!m) return;
    let photoid = m[1];
    let photourl = `https://www.instagram.com/p/${photoid}/media/?size=l`;

    // Fetch photo.
    let r = await context.fetch(photourl);
    let data = await r.blob();

    // Save photo to drive.
    let file = new File([data], "ig-" + photoid + ".jpg");
    let driveurl = await Drive.save(file);

    context.topic.add(n_media, driveurl);
    context.updated(context.topic);
    return true;
  }
};


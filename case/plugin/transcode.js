// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for transcoding videos.

import {store, settings} from "/case/app/global.js";
import {write_to_drive} from "/case/app/drive.js";

const n_media = store.lookup("media");

export default class TranscodePlugin {
  async process(action, url, context) {
    if (!context.topic) return false;
    console.log("transcode", url);

    // Get base filename.
    let fn = new URL(url).pathname;
    if (fn.startsWith("/file/")) fn = fn.substring(6);
    if (fn.endsWith("/file")) fn = fn.substring(0, fn.length - 5);
    if (fn.endsWith(".wmv") || fn.endsWith(".avi")) {
      fn = fn.substring(0, fn.length - 4) + ".mp4";
    }

    // Transcode video.
    let r = await fetch(context.service("transcode", {url}));
    let data = await r.arrayBuffer();

    // Write transcoded video to drive.
    console.log("write to drive", fn, data.length);
    let drive_url = await write_to_drive(fn, data);

    console.log(`add video ${drive_url} to topic ${context.topic.id}`);
    context.topic.put(n_media, drive_url);
    context.updated(context.topic);
    return true;
  }
};


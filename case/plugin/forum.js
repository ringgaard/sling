// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for photos from forum post to topics.

import {store, settings} from "/case/app/global.js";

const n_is = store.lookup("is");
const n_media = store.lookup("media");
const n_has_quality = store.lookup("P1552");
const n_not_safe_for_work = store.lookup("Q2716583");

// Image downloaders for image hosts.
const images_services = [

{
  pattern: /(member|newreply|showthread)\.php\?/,
  fetch: (url, context) => null,
},

{
  pattern: /http:\/\/img\d+\.imagevenue\.com\/img\.php\?/,
  fetch: async (url, context) => {
    let r = await fetch(context.proxy(url));
    let html = await r.text();
    let doc = new DOMParser().parseFromString(html, "text/html");
    let img = doc.querySelector(".card-body img");
    return img && img.src ? img.src : null;
  },
  nsfw: true,
},

{
  pattern: /https:\/\/pixhost\.to\//,
  fetch: async (url, context) => {
    let r = await fetch(context.proxy(url));
    let html = await r.text();
    let doc = new DOMParser().parseFromString(html, "text/html");
    let img = doc.getElementById("image");
    return img && img.src ? img.src : null;
  },
  nsfw: true,
},

{
  pattern: /https?:\/\/pimpandhost\.com\/image\//,
  fetch: async (url, context) => {
    let r = await fetch(context.proxy(url));
    let html = await r.text();
    let doc = new DOMParser().parseFromString(html, "text/html");
    let img = doc.querySelector("img.normal");
    return img && img.src ? img.src : null;
  },
  nsfw: true,
},

{
  pattern: /https?:\/\/imgbox\.com\//,
  fetch: async (url, context) => {
    let r = await fetch(context.proxy(url + "?full=1"));
    let html = await r.text();
    let doc = new DOMParser().parseFromString(html, "text/html");
    let meta = doc.querySelector('meta[property="og:image"]');
    return meta && meta.content ? meta.content : null;
  },
  nsfw: true,
},

{
  pattern: /https?:\/\/www\.imagebam\.com\/image\//,
  fetch: async (url, context) => {
    let r = await fetch(context.proxy(url + "?full=1"));
    let html = await r.text();
    let doc = new DOMParser().parseFromString(html, "text/html");
    let img = doc.querySelector("img.main-image");
    return img && img.src ? img.src : null;
  },
  nsfw: true,
},

{
  pattern: /https?:\/\/postimg\.(cc|org)\/image\//,
  fetch: async (url, context) => {
    let r = await fetch(context.proxy(url));
    let html = await r.text();
    let doc = new DOMParser().parseFromString(html, "text/html");
    let img = doc.querySelector("#main-image");
    return img && img.src ? img.src : null;
  },
  nsfw: true,
},

];

export default class AlbumPlugin {
  async process(action, url, context) {
    let topic = context.topic;
    if (!topic) return false;
    console.log(`Add photos from ${url} to topic ${context.topic.id}`);

    // Fetch forum post.
    let r = await fetch(context.proxy(url));
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");

    // Find all image links.
    let num_images = 0;
    let seen = new Set();
    for (let link of doc.getElementsByTagName("a")) {
      // Get image link.
      if (!link.querySelector("img")) continue;
      let href = link.getAttribute("href");
      if (!href) continue;
      if (seen.has(href)) continue;
      seen.add(href);

      // Find service for fetching image.
      let service = null;
      for (let s of images_services) {
        if (href.match(s.pattern)) {
          service = s;
          break;
        }
      }

      // Fetch image.
      if (service) {
        let photo = await service.fetch(href, context);
        if (photo) {
          console.log("photo", photo, "href", href);
          // Check for duplicate.
          let dup = false;
          for (let media of context.topic.all(n_media)) {
            if (store.resolve(media) == photo) {
              dup = true;
              break;
            }
          }

          // Add image to topic.
          if (!dup) {
            if (service.nsfw) {
              let media = store.frame();
              media.add(n_is, photo);
              media.add(n_has_quality, n_not_safe_for_work);
              context.topic.add(n_media, media);
            } else {
              context.topic.add(n_media, photo);
            }
            num_images++;
          }
        }
      } else {
        console.log("no service for fetching", href);
      }
    }

    return num_images > 0;
  }
};


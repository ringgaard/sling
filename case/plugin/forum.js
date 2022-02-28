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
  pattern: /https?:\/\/\w+\.imagevenue\.com\//,
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
  pattern: /https?:\/\/(www\.)?pimpandhost\.com\/image\//,
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
  pattern: /https?:\/\/www\.imagebam\.com\/(image|view)\//,
  fetch: async (url, context) => {
    let r = await fetch(context.proxy(url + "?full=1"), {
      headers: {"XCookie": "nsfw_inter=1"},
    });
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

{
  pattern: /https?:\/\/ibb\.co\//,
  fetch: async (url, context) => {
    let r = await fetch(context.proxy(url));
    let html = await r.text();
    let doc = new DOMParser().parseFromString(html, "text/html");
    let container = doc.getElementById("image-viewer-container");
    let img = container.querySelector("img");
    return img && img.src ? img.src : null;
  },
  nsfw: true,
},

{
  pattern: /https?:\/\/www\.turboimagehost\.com\//,
  fetch: async (url, context) => {
    let r = await fetch(context.proxy(url));
    let html = await r.text();
    let doc = new DOMParser().parseFromString(html, "text/html");
    let img = doc.querySelector("img.uImage");
    return img && img.src ? img.src : null;
  },
  nsfw: true,
},

{
  pattern: /https?:\/\/lemmecheck\.tube\//,
  fetch: async (url, context) => {
    let r = await fetch(context.proxy(url));
    let html = await r.text();
    let doc = new DOMParser().parseFromString(html, "text/html");
    let container = doc.querySelector("div.gallery-block");
    let img = container.querySelector("img");
    if (!img) return null;
    return new URL(img.getAttribute("src"), url).href;
  },
  nsfw: true,
},

{
  pattern: /https?:\/\/\w+rater\.com\//,
  fetch: async (url, context) => {
    let r = await fetch(context.proxy(url));
    let html = await r.text();
    let doc = new DOMParser().parseFromString(html, "text/html");
    let ogimage = doc.querySelector('meta[property="og:image"]');
    if (ogimage) {
      let imgurl = ogimage.getAttribute("content");
      let q = imgurl.indexOf("?");
      if (q != -1) imgurl = imgurl.substring(0, q);
      return imgurl;
    }
  },
  nsfw: true,
},

{
  pattern: /https?:\/\/www\.galleries\./,
  fetch: async (url, context) => {
    let r = await fetch(context.proxy(url));
    let html = await r.text();
    let doc = new DOMParser().parseFromString(html, "text/html");
    let container = doc.querySelector("a");
    let img = container.querySelector("img");
    if (!img) return null;
    return new URL(img.getAttribute("src"), url).href;
  },
  nsfw: true,
},

{
  pattern: /https?:\/\/celeb\.gate\.cc\//,
  fetch: async (url, context) => {
    let r = await fetch(context.proxy(url));
    let html = await r.text();
    let doc = new DOMParser().parseFromString(html, "text/html");
    let img = doc.querySelector("img.img-fluid");
    if (!img) return null;
    return img.getAttribute("src");
  },
  nsfw: true,
},

{
  pattern: /https?:\/\/postimage.org\/image\//,
  fetch: async (url, context) => {
    let r = await fetch(context.proxy(url));
    let html = await r.text();
    let doc = new DOMParser().parseFromString(html, "text/html");
    let img = doc.getElementById("main-image");
    if (!img) return null;
    return img.getAttribute("src");
  },
  nsfw: true,
},

{
  pattern: /https?:\/\/imagetwist\.com\//,
  fetch: async (url, context) => {
    let r = await fetch(context.proxy(url));
    let html = await r.text();
    let doc = new DOMParser().parseFromString(html, "text/html");
    let img = doc.querySelector("img.pic.img.img-responsive");
    if (!img) return null;
    return img.getAttribute("src");
  },
  nsfw: true,
},

{
  pattern: /https?\:\/\/[A-Za-z0-9\.\-]+\/(uploads|galleries)\//,
  fetch: (url, context) => url,
  nsfw: true,
},

{
  pattern: /https?\:\/\/.+\.jpe?g$/,
  fetch: (url, context) => url,
  nsfw: true,
},

];

let gallery_containers = [
  "div.gallery",
  "div.image-gallery",
  "div.actual-gallery-container",
  "div.gallery-block",
  "div.mainGalleryDiv",
  "div.galleryGrid",
  "div.player-right",
  "div.gallery-wrapper",
];

export default class AlbumPlugin {
  async process(action, url, context) {
    let topic = context.topic;
    if (!topic) return false;
    console.log(`Add photos from ${url} to topic ${context.topic.id}`);

    // Fetch forum post.
    let r = await fetch(context.proxy(url), {headers: {
      "XUser-Agent": navigator.userAgent,
    }});
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");

    // Find image gallery container.
    var container;
    for (let selector of gallery_containers) {
      container = doc.querySelector(selector);
      if (container) break;
    }
    if (!container) container = doc;

    // Find links to images.
    let hrefs = new Array();
    let gallery = container.querySelectorAll("div.galleryPics");
    if (gallery.length > 0) {
      for (let photo of gallery.values()) {
        let link = photo.querySelector("a");
        if (link) {
          let href = link.getAttribute("href");
          if (!href) continue;
          href = new URL(href, url).href;
          if (hrefs.includes(href)) continue;
          hrefs.push(href);
        }
      }
    } else {
      for (let link of container.getElementsByTagName("a")) {
        // Get image link.
        if (!link.querySelector("img")) continue;
        let href = link.getAttribute("href");
        if (!href) continue;
        try {
          href = new URL(href, url).href;
        } catch (error) {
          console.log("Unable to parse URL", href);
          continue;
        }
        if (hrefs.includes(href)) continue;
        hrefs.push(href);
      }
    }

    let slides = container.querySelectorAll("div.slide");
    if (slides.length > 0) {
      for (let photo of slides.values()) {
        let img = photo.querySelector("img");
        if (img) {
          let href = img.getAttribute("data-src");
          if (!href) continue;
          href = new URL(href, url).href;
          if (hrefs.includes(href)) continue;
          hrefs.push(href);
        }
      }
    }

    // Retrieve images.
    let num_images = 0;
    for (let href of hrefs) {
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

    context.updated(topic);
    return num_images > 0;
  }
};


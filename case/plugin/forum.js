// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for photos from forum post to topics.

import {store, frame, settings} from "/common/lib/global.js";
import {MD5} from "/common/lib/hash.js";

const n_is = frame("is");
const n_media = frame("media");

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
  convert: (thumb) => {
    let base = thumb.slice(thumb.lastIndexOf("/") + 1);
    let hires = base.replace("_t", "_o");
    let d = MD5(hires);
    let path = `${d[0]}${d[1]}/${d[2]}${d[3]}/${d[4]}${d[5]}`
    let photo = "https://cdn-images.imagevenue.com/" +
                path + "/" + hires;
    return photo;
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
  convert: (thumb) => {
    return thumb.replace("thumbs", "images").replace("_t", "_o");
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
  convert: (thumb) => {
    let m = thumb.match(
      /https\:\/\/thumbs(\d+)\.imagebam\.com\/\w+\/\w+\/\w+\/(.*)/);
    let hostno = m[1];
    let base = m[2];
    let hires = base.replace("_t", "_o");
    let d = MD5(hires);
    let path = `${d[0]}${d[1]}/${d[2]}${d[3]}/${d[4]}${d[5]}`
    let photo = `https://images${hostno}.imagebam.com/${path}/${hires}`;
    return photo;
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
    let r = await fetch(context.proxy(url), {headers: {
      "XUser-Agent": navigator.userAgent,
    }});
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
  pattern: /https?:\/\/imageupper\.com\//,
  fetch: async (url, context) => {
    let r = await fetch(context.proxy(url));
    let html = await r.text();
    let doc = new DOMParser().parseFromString(html, "text/html");
    let img = doc.querySelector("img.img");
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
  "ul.images",
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
    let r = await context.fetch(url);
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
    let thumbs = new Map();
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
        let thumb = link.querySelector("img");
        if (!thumb) continue;
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
        let src = thumb.getAttribute("src");
        if (src) thumbs.set(href, src);
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
        let photo;
        if (service.convert) {
          let thumb = thumbs.get(href);
          if (thumb) photo = service.convert(thumb);
        }
        if (!photo) {
          photo = await service.fetch(href, context);
        }
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
            if (service.nsfw) photo = "!" + photo;
            context.topic.add(n_media, photo);
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


// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Photo gallery web component.

import {Component} from "./component.js";
import {MdModal} from "./material.js";

export var mediadb = {
  enabled: true,
  thumb: true,
  mediasvc: "/media/",
  thumbsvc: "/thumb/",
}

const photo_sources = {
  "upload.wikimedia.org": "wikimedia.org",
  "pbs.twimg.com": "twitter.com",
  "redd.it": "reddit.com",
  "pinimg.com": "pinterest.com",
  "live.staticflickr.com": "flickr.com",
  "iv1.lisimg.com": "listal.com",
  "ilarge.lisimg.com": "listal.com",
  "lthumb.lisimg.com": "listal.com",
  "gstatic.com": "google.com",
  "media-amazon.com": "imdb.com",
}

function mod(m, n) {
  return ((m % n) + n) % n;
}

export function imageurl(url, thumb) {
  if (mediadb.enabled) {
    let escaped = encodeURIComponent(url);
    escaped = escaped.replace(/%3A/g, ":").replace(/%2F/g, "/");
    if (thumb && mediadb.thumb && !url.endsWith(".svg")) {
      return mediadb.thumbsvc + escaped;
    } else {
      return mediadb.mediasvc + escaped;
    }
  } else {
    return url;
  }
}

export function censor(gallery, nsfw) {
  let filtered = [];
  let urls = new Set();
  for (let image of gallery) {
    if (!image.url) continue;
    if (!nsfw && image.nsfw) continue;
    if (urls.has(image.url)) continue;
    if (image.url.endsWith(".tif") || image.url.endsWith(".tiff")) continue;
    filtered.push(image);
    urls.add(image.url);
  }
  return filtered;
}

export class PhotoGallery extends MdModal {
  constructor() {
    super();
    this.first = true;
    this.edited = false;
  }

  onconnected() {
    this.attach(this.onclick, "click", ".photo");
    this.attach(this.onprev, "click", ".prev");
    this.attach(this.onnext, "click", ".next");
    this.attach(this.onfullscreen, "click", "#fullscreen");
    this.attach(this.close, "click", "#close");
    this.attach(this.onsource, "click", ".domain");
    this.attach(this.onkeypress, "keydown");
  }

  onupdate() {
    this.current = 0;
    this.photos = [];
    for (let image of this.state) {
      this.photos.push({
        url: image.url,
        caption: image.text,
        nsfw: image.nsfw,
        image: null
      });
    }

    if (this.photos.length > 0) {
      this.preload(this.current, 1);
      this.display(this.current);
    }
  }

  onkeypress(e) {
    if (e.keyCode == 37) {
      this.onprev(e);
    } else if (e.keyCode == 39) {
      this.onnext(e);
    } else if (e.keyCode == 27) {
      this.close(e);
    } else if (e.keyCode == 88) {
      this.flipnsfw(e);
    } else if (e.keyCode == 68) {
      this.delimg(e);
    } else if (e.keyCode == 70) {
      this.onfullscreen(e);
    }
    this.focus();
  }

  onload(e) {
    let image = e.target;
    image.style.cursor = null;
    let photo = image.photo;
    photo.width = image.naturalWidth;
    photo.height = image.naturalHeight;
    if (photo == this.photos[this.current]) {
      let w = photo.width;
      let h = photo.height;
      this.find(".size").update(w && h ? `${w} x ${h}`: null);
    }
    if (this.first) {
      this.first = false;
      this.preload(this.current, 1);
    }
  }

  onclick(e) {
    let url = imageurl(this.photos[this.current].url, false);
    window.open(url, "_blank", "noopener,noreferrer");
  }

  onsource(e) {
    let url = this.photos[this.current].url;
    window.open(url, "_blank", "noopener,noreferrer");
    e.stopPropagation();
  }

  flipnsfw(e) {
    let photo = this.photos[this.current];
    photo.nsfw = !photo.nsfw;
    this.dispatch(photo.nsfw ? "nsfw" : "sfw", photo.url);
    this.find(".nsfw").update(photo.nsfw ? "NSFW" : null);
    this.edited = true;
  }

  delimg(e) {
    let photo = this.photos[this.current];
    this.dispatch("delimage", photo.url);
    this.photos.splice(this.current, 1);
    this.edited = true;
    this.move(0);
  }

  onprev(e) {
    this.move(-this.stepsize(e), e.altKey);
    e.stopPropagation();
    if (e.altKey) e.preventDefault();
  }

  onnext(e) {
    this.move(this.stepsize(e), e.altKey);
    e.stopPropagation();
    if (e.altKey) e.preventDefault();
  }

  onfullscreen(e) {
    e.stopPropagation();
    if (!document.fullscreenElement) {
      this.requestFullscreen();
    } else {
      document.exitFullscreen();
    }
  }

  onclose(e) {
    if (e) e.stopPropagation();
    if (this.edited) this.dispatch("picedit");
  }

  stepsize(e) {
    if (e.shiftKey) return 10;
    if (e.ctrlKey) return 100;
    return 1;
  }

  move(n, nsfw) {
    let size = this.photos.length;
    this.current = mod(this.current + n, size);
    if (nsfw) {
      let i = this.current;
      while (!this.photos[i].nsfw) {
        i = mod(i + (n > 0 ? 1 : -1), size);
        if (i == this.current) break;
      }
      this.current = i;
    }
    if (size > 0) {
      this.preload(this.current, n);
      this.display(this.current);
    } else {
      this.close();
    }
  }

  display(n) {
    let photo = this.photos[n];
    let caption = photo.caption;
    if (caption) {
      caption = caption.toString().replace(/\[\[|\]\]/g, "");
    }
    this.find(".image").replaceWith(photo.image);
    this.find(".caption").update(caption);
    let counter = `${this.current + 1} / ${this.photos.length}`;
    this.find(".counter").update(counter);

    let url = new URL(photo.url);
    let domain = url.hostname;
    if (domain.startsWith("www.")) domain = domain.slice(4);
    if (domain.startsWith("m.")) domain = domain.slice(2);
    if (domain.startsWith("i.")) domain = domain.slice(2);
    if (domain in photo_sources) {
      domain = photo_sources[domain];
    } else {
      let dot = domain.indexOf('.');
      if (dot != -1) {
        let top = domain.slice(dot + 1);
        if (top in photo_sources) domain = photo_sources[top];
      }
    }

    let copyrighted = true;
    if (domain == "wikimedia.org") {
      let m = url.pathname.match(/\/wikipedia\/(\w+)\/.+/);
      if (m[1] && m[1] != "commons") {
        domain += " (" + m[1] + ")";
      }
      copyrighted = false;
    }

    this.find(".domain").update(domain);
    this.find(".nsfw").update(photo.nsfw ? "NSFW" : null);
    this.find(".copyright").update(copyrighted);
    if (photo.width && photo.height) {
      let w = photo.width;
      let h = photo.height;
      this.find(".size").update(w && h ? `${w} x ${h}`: null);
    }
  }

  preload(position, direction) {
    let lookahead = this.first ? 1 : 5;
    for (var i = 0; i < lookahead; ++i) {
      let n = mod(position + i * direction, this.photos.length);
      if (this.photos[n].image == null) {
        let url = this.photos[n].url;
        var viewer;
        if (url.endsWith(".mp4") || url.endsWith(".webm")) {
          viewer = document.createElement('video');
          viewer.controls = true;
        } else {
          viewer = new Image();
          viewer.style.cursor = "wait";
          if (url.endsWith(".svg")) {
            viewer.style.background = "white";
            viewer.style.padding = "10px";
          }
        }
        viewer.src = imageurl(url, false);
        viewer.classList.add("image");
        viewer.referrerPolicy = "no-referrer";
        viewer.addEventListener("load", e => this.onload(e));
        this.photos[n].image = viewer;
        viewer.photo = this.photos[n];
      }
    }
  }

  render() {
    if (this.state) return null;
    return `
      <div class="content">
        <div class="photo">
          <img class="image" referrerpolicy="no-referrer">
          <md-text class="size"></md-text>
          <div class="toolbox">
            <md-icon-button id="fullscreen" icon="fullscreen">
            </md-icon-button>
            <md-icon-button id="close" icon="close">
            </md-icon-button>
          </div>

          <div class="source">
            <md-text class="domain"></md-text>
            <photo-copyright class="copyright"></photo-copyright>
            <md-text class="nsfw"></md-text>
          </div>
          <md-text class="counter"></md-text>
          <a class="prev">&#10094;</a>
          <a class="next">&#10095;</a>
        </div>
        <md-text class="caption"></md-text>
      </div>
    `;
  }

  static stylesheet() {
    return `
      $ {
        background-color: #0E0E0E;
        user-select: none;
      }

      $ .content {
        position: relative;
        margin: auto;
        padding: 0;
        width: 100%;
        height: 100%;
      }

      $ .photo {
        margin: auto;
      }

      $ .image {
        display: block;
        position: absolute;
        top: 0;
        bottom: 0;
        left: 0;
        right: 0;
        max-width: 100%;
        max-height: 100%;
        width: auto;
        height: auto;
        margin: auto;
        cursor: pointer;
        background-color: hsl(0, 0%, 90%);
      }

      $ .counter {
        position: absolute;
        top: 0;
        left: 0;

        color: rgb(255, 255, 255);
        font-size: 12px;
        padding: 8px 12px;
      }

      $ .toolbox {
        display: flex;
        position: absolute;
        top: 0;
        right: 0;
        color: white;
      }

      $ .toolbox md-icon-button:hover {
        color: #999;
        text-decoration: none;
        cursor: pointer;
      }

      $ .source {
        position: absolute;
        bottom: 0;
        left: 0;

        display: flex;
        align-items: center;

        color: rgb(255, 255, 255);
        font-size: 12px;
        padding: 8px 12px;
        height: 14px;
        cursor: pointer;
      }

      $ a {
        color: white;
        text-decoration: none;
        cursor: pointer;
        outline: none;
      }

      $ .nsfw {
        border-radius: 3px;
        border: 1px solid;
        font-size: 12px;
        padding: 2px 4px;
        margin: 2px;
        color: #d10023;
      }

      $ .size {
        position: absolute;
        bottom: 0;
        right: 0;

        color: rgb(255, 255, 255);
        font-size: 12px;
        padding: 8px 12px;
      }

      $ .prev, $ .next {
        cursor: pointer;
        width: auto;
        padding: 16px;
        color: white;
        font-weight: bold;
        font-size: 20px;
        transition: 0.6s ease;
        background-color: rgba(0, 0, 0, 0.2);
      }

      $ .next {
        position: absolute;
        right: 0;
        top: 50%;
        border-top-left-radius: 10px;
        border-bottom-left-radius: 10px;
      }

      $ .prev {
        position: absolute;
        left: 0;
        top: 50%;
        border-top-right-radius: 5px;
        border-bottom-right-radius: 5px;
      }

      $ .prev:hover, $ .next:hover {
        background-color: rgba(0, 0, 0, 0.8);
      }

      $ .caption {
        position: absolute;
        bottom: 0;
        left: 50%;
        transform: translate(-50%, -50%);
        color: white;
        background: rgba(0, 0, 0, 0.5);
        padding: 10px;
      }
    `;
  }
}

Component.register(PhotoGallery);

class PhotoCopyright extends Component {
  visible() {
    return this.state;
  }

  render() {
    return `<md-icon
              icon="copyright"
              title="Image may be subject to copyright">
            </md-icon>`;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        vertical-align: middle;
        align-items: center;
      }
      $ md-icon {
        font-size: 16px;
        margin: 2px;
      }
    `;
  }
}

Component.register(PhotoCopyright);


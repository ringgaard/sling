// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Photo gallery web component.

import {Component} from "./component.js";
import {MdModal} from "./material.js";

const IMAGE = 1;
const VIDEO = 2;
const GRAPHICS = 3;
const DOCUMENT = 4;

const media_types = {
  ".jpeg": IMAGE,
  ".jpg": IMAGE,
  ".gif": IMAGE,
  ".png": IMAGE,
  ".webp": IMAGE,
  ".mp4": VIDEO,
  ".mpeg": VIDEO,
  ".webm": VIDEO,
  ".svg": GRAPHICS,
  ".pdf": DOCUMENT,
}

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

export function mediatype(url) {
  let base = url.lastIndexOf("/");
  if (base != -1) url = url.substring(base);
  let pos = url.lastIndexOf(".");
  if (pos == -1) return IMAGE;
  return media_types[url.substring(pos).toLowerCase()];
}

export function isimage(url) {
  return mediatype(url) == IMAGE;
}

export function imageurl(url, thumb) {
  if (mediadb.enabled) {
    let escaped = encodeURIComponent(url);
    escaped = escaped.replace(/%3A/g, ":").replace(/%2F/g, "/");
    if (mediatype(url) == IMAGE) {
      if (thumb && mediadb.thumb) {
        return mediadb.thumbsvc + escaped;
      } else {
        return mediadb.mediasvc + escaped;
      }
    }
  }
  return url;
}

export function hasthumb(url) {
  return mediatype(url) == IMAGE;
}

export function censor(gallery, nsfw) {
  let filtered = [];
  let urls = new Set();
  for (let image of gallery) {
    if (!image.url) continue;
    if (!nsfw && image.nsfw) continue;
    if (urls.has(image.url)) continue;
    if (!mediatype(image.url)) continue;
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
    this.zoom = 1;
  }

  onconnected() {
    this.attach(this.onprev, "click", ".prev");
    this.attach(this.onnext, "click", ".next");
    this.attach(this.onopennew, "click", "#open");
    this.attach(this.onfullsize, "click", "#fullsize");
    this.attach(this.onfullscreen, "click", "#fullscreen");
    this.attach(this.onzoomin, "click", "#zoomin");
    this.attach(this.onzoomout, "click", "#zoomout");
    this.attach(this.onrefresh, "click", "#refresh");
    this.attach(this.oncopyurl, "click", "#copyurl");
    this.attach(this.onsearch, "click", "#search");
    this.attach(this.close, "click", "#close");
    this.attach(this.onsource, "click", ".domain");
    this.attach(this.oncut, "cut");
    this.attach(this.oncopy, "copy");
    this.attach(this.onpaste, "paste");
    this.attach(this.onkeypress, "keydown");
  }

  onupdate() {
    this.current = 0;
    this.photos = [];
    for (let image of this.state) {
      if (image.start) {
        this.current = this.photos.length;
      }
      this.photos.push({
        url: image.url,
        caption: image.text,
        nsfw: image.nsfw,
        image: null,
        selected: false,
        loaded: false,
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
    } else if (e.keyCode == 88 && !e.ctrlKey) {
      if (e.shiftKey) {
        this.flipallnsfw(e);
      } else {
        this.flipnsfw(e);
      }
    } else if (e.keyCode == 68) {
      this.delimg(e);
    } else if (e.keyCode == 70) {
      this.onfullscreen(e);
    } else if (e.keyCode == 90) {
      this.onfullsize(e);
    } else if (e.keyCode == 83) {
      this.onselect(e);
    } else if (e.keyCode == 82) {
      this.onrefresh(e);
    } else if (e.keyCode == 71) {
      this.ongallery(e);
    }
  }

  onload(e) {
    let image = e.target;
    let photo = image.photo;
    photo.width = image.naturalWidth;
    photo.height = image.naturalHeight;
    photo.loaded = true;
    if (photo == this.photos[this.current]) {
      let w = photo.width;
      let h = photo.height;
      this.find(".size").update(w && h ? `${w} x ${h}`: null);
      this.find(".photo").style.cursor = "";
    }
    if (this.first) {
      this.first = false;
      this.preload(this.current, 1);
    }
  }

  onopennew(e) {
    let url = imageurl(this.photos[this.current].url, false);
    window.open(url, "_blank", "noopener,noreferrer");
  }

  onsearch(e) {
    let url = this.photos[this.current].url
    if (e.ctrlKey) url = "https://ringgaard.com/media/" + url;
    window.open(`https://lens.google.com/uploadbyurl?url=${url}`,
                 "_blank", "noopener,noreferrer");
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

  flipallnsfw(e) {
    let nsfw = !this.photos[this.current].nsfw;
    for (let photo of this.photos) {
      if (photo.nsfw == nsfw) continue;
      photo.nsfw = nsfw;
      this.dispatch(nsfw ? "nsfw" : "sfw", photo.url);
      this.edited = true;
    }
    this.find(".nsfw").update(nsfw ? "NSFW" : null);
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
    let photo = this.find(".photo");
    if (!photo.fullscreenElement) {
      photo.requestFullscreen();
    } else {
      document.exitFullscreen();
    }
    photo.focus();
  }

  onfullsize(e) {
    e.stopPropagation();
    let photo = this.find(".photo");
    photo.classList.toggle("full");
    this.display(this.current);
  }

  onzoomin(e) {
    e.stopPropagation();
    this.find(".photo").classList.add("full");
    this.zoom += 0.25;
    this.display(this.current);
  }

  onzoomout(e) {
    e.stopPropagation();
    this.find(".photo").classList.add("full");
    if (this.zoom >= 0.5) this.zoom -= 0.25;
    this.display(this.current);
  }

  onrefresh(e) {
    e.stopPropagation();
    let photo = this.photos[this.current];
    photo.image.src = imageurl(photo.url, false);
  }

  oncopyurl(e) {
    e.stopPropagation();
    let photo = this.photos[this.current];
    navigator.clipboard.writeText(photo.url);
  }

  onselect(e) {
    let photo = this.photos[this.current];
    if (photo.selected) {
      this.find("#flag").classList.add("unmarked");
      photo.selected = false;
    } else {
      this.find("#flag").classList.remove("unmarked");
      photo.selected = true;
    }
  }

  gallery(remove) {
    let selected = new Array();
    let removed = new Set();
    for (let photo of this.photos) {
      if (photo.selected) {
        let url = photo.url.replace(/\s/g, "%20");
        if (photo.nsfw) url = "!" + url;
        selected.push(url);
        photo.selected = false;
        if (remove) removed.add(photo);
      }
    }

    if (selected.length == 0) {
      let photo = this.photos[this.current];
      let url = photo.url.replace(/\s/g, "%20");
      if (photo.nsfw) url = "!" + url;
      selected.push(url);
      if (remove) removed.add(photo);
    }

    if (selected.length > 0) {
      // Put gallery URL in clipboard.
      navigator.clipboard.writeText("gallery:" + selected.join(" "));

      // Remove selected photos if requested.
      if (remove) {
         this.photos = this.photos.filter(photo => {
           if (removed.has(photo)) {
             this.dispatch("delimage", photo.url);
             return false;
           } else {
             return true;
           }
         });
         this.edited = true;
         this.move(0);
      }
    }
  }

  ongallery(e) {
    let remove = e.shiftKey;
    this.gallery(remove);
  }

  oncut(e) {
    this.gallery(true);
  }

  oncopy(e) {
    this.gallery(false);
  }

  async onpaste(e) {
    if (!navigator.clipboard) throw "Access to clipboard denied";
    let clipboard = await navigator.clipboard.readText();

    let pos = this.current;
    let anchor = this.photos[pos];
    let photos = [];
    if (clipboard.startsWith("gallery:")) {
      let gallery = clipboard.slice(8).split(" ");
      for (let url of gallery) {
        url = url.trim();
        let nsfw = false;
        if (url.startsWith("!")) {
          url = url.slice(1);
          nsfw = true;
        }
        photos.push({url, nsfw});
      }
    } else if (isimage(clipboard)) {
      photos.push({url: clipboard});
    }

    if (photos.length > 0) {
      this.photos.splice(pos, 0, ...photos);
      this.move(0);
      this.dispatch("insimage", {anchor: anchor.url, photos});
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
    if (mediatype(photo.url) == IMAGE) {
      this.find(".photo").style.cursor = photo.loaded ? "" : "wait";
    }
    this.find(".caption").update(caption);
    let counter = `${this.current + 1} / ${this.photos.length}`;
    this.find(".counter").update(counter);

    let fullsize = this.find(".photo").classList.contains("full");
    if (fullsize) {
      photo.image.style.transform = `scale(${this.zoom})`;
      photo.image.style.transformOrigin = "0 0";
    } else {
      photo.image.style = "";
    }

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
      let zoom = fullsize ? ` (${Math.round(this.zoom * 100)}%)` : "";
      this.find(".size").update(w && h ? `${w} x ${h}` + zoom : null);
    }

    if (photo.selected) {
      this.find("#flag").classList.remove("unmarked");
    } else {
      this.find("#flag").classList.add("unmarked");
    }

    if (mediatype(photo.url) == DOCUMENT) {
      this.find(".toolbox").style.display = "none";
    } else {
      this.find(".toolbox").style.display = "flex";
    }
  }

  preload(position, direction) {
    let lookahead = this.first ? 1 : 3;
    for (let i = 0; i < lookahead; ++i) {
      let n = mod(position + i * direction, this.photos.length);
      let photo = this.photos[n];
      if (photo.image == null) {
        let url = this.photos[n].url;
        let type = mediatype(url);
        var viewer;
        if (type == VIDEO) {
          viewer = document.createElement("video");
          viewer.controls = true;
        } else if (type == DOCUMENT) {
          viewer = document.createElement("embed");
          viewer.type = "application/pdf";
          viewer.classList.add("doc");
        } else {
          viewer = new Image();
          if (type == GRAPHICS) {
            viewer.style.background = "white";
            viewer.style.padding = "10px";
          }
        }
        viewer.src = imageurl(url, false);
        viewer.classList.add("image");
        viewer.referrerPolicy = "no-referrer";
        viewer.addEventListener("load", e => this.onload(e));
        photo.image = viewer;
        viewer.photo = photo;
      }
    }
  }

  render() {
    if (this.state) return null;
    return `
      <div class="photo" tabindex="0">
        <img class="image" referrerpolicy="no-referrer">
      </div>
      <md-text class="size"></md-text>
      <div class="toolbox">
        <md-icon-button id="zoomout" icon="zoom_out">
        </md-icon-button>
        <md-icon-button id="zoomin" icon="zoom_in">
        </md-icon-button>
        <md-icon-button id="refresh" icon="refresh">
        </md-icon-button>
        <md-icon-button id="open" icon="open_in_new">
        </md-icon-button>
        <md-icon-button id="fullsize" icon="fit_screen">
        </md-icon-button>
        <md-icon-button id="fullscreen" icon="fullscreen">
        </md-icon-button>
        <md-icon-button id="copyurl" icon="content_copy">
        </md-icon-button>
        <md-icon-button id="search" icon="image_search">
        </md-icon-button>
        <md-icon-button id="close" icon="close">
        </md-icon-button>
      </div>
      <div class="source">
        <md-text class="domain"></md-text>
        <photo-copyright class="copyright"></photo-copyright>
        <md-text class="nsfw"></md-text>
        <md-icon id="flag" icon="flag"></md-icon>
      </div>
      <md-text class="counter"></md-text>
      <a class="prev">&#10094;</a>
      <a class="next">&#10095;</a>
      <md-text class="caption"></md-text>
    `;
  }

  static stylesheet() {
    return `
      $ {
        position: absolute;
        background-color: #0E0E0E;
        user-select: none;
        width: 100%;
        height: 100%;
      }

      $ .photo {
        position: relative;
        display: flex;
        width: 100%;
        height: 100%;
        margin: auto;
      }

      $ .image {
        display: block;
        max-width: 100%;
        max-height: 100%;
        width: auto;
        height: auto;
        margin: auto;
        background-color: hsl(0, 0%, 90%);
      }

      $ .doc {
        width: 100%;
        height: 100%;
      }

      $ .full {
        display: block;
        overflow: auto;
      }

      $ .full::-webkit-scrollbar {
        background-color: #0E0E0E;
        width: 8px;
        height: 8px;
      }

      $ .full::-webkit-scrollbar-corner {
        background-color: #0E0E0E;
      }

      $ .full::-webkit-scrollbar-thumb {
        background-color: #A0A0A0;
        border-radius: 4px;
      }

      $ .full::-webkit-scrollbar-thumb:hover {
        background-color: #F0F0F0;
      }

      $ .full .image {
        display: block;
        max-width: initial;
        max-height: initial;
      }

      $ .counter {
        position: absolute;
        top: 0;
        left: 0;

        color: #FFFFFF;
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

      $ .unmarked {
        visibility: hidden;
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
        border-radius: 10px;
      }

      $ .next {
        position: absolute;
        right: 0;
        top: 50%;
        margin-right: 8px;
      }

      $ .prev {
        position: absolute;
        left: 0;
        top: 50%;
        margin-left: 8px;
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

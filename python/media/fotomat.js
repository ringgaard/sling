import {Component} from "/common/lib/component.js";
import {MdApp, MdCard, inform} from "/common/lib/material.js";
import {PhotoGallery, imageurl} from "/common/lib/gallery.js";

class FotomatApp extends MdApp {
  onconnected() {
    this.bind("#query", "keyup", e => {
      if (e.key === "Enter") this.onquery(e);
    });
    this.bind(null, "keyup", e => {
      if (e.key === "Escape") this.find("#query").focus();
    });
    this.bind("#search", "click", e => this.onquery(e));
    this.bind("#save", "click", e => this.onsave(e));
    this.bind("#copy", "click", e => this.oncopy(e));
    this.bind("#onlydups", "change", e => this.find("#photos").refresh());
    this.mark_clean();
    window.addEventListener("beforeunload", e => this.onbeforeunload(e));
  }

  onbeforeunload(e) {
    if (this.dirty) {
      e.preventDefault();
      e.returnValue = "";
    }
  }

  mark_clean() {
    this.dirty = false;
    this.find("#save").disable();
  }

  mark_dirty() {
    if (this.profile.item) {
      this.dirty = true;
      this.find("#save").enable();
    }
  }

  async onquery(e) {
    let query = this.find("#query").value().trim();

    this.style.cursor = "wait";
    this.find("#photos").update(null);
    this.mark_clean(true);
    this.profile = null;

    try {
      let r = await fetch(`/fotomat/fetch?q=${encodeURIComponent(query)}`);
      this.profile = await r.json();
      this.find("#photos").update(this.profile);
      this.find("md-content").scrollTop = 0;
    } catch (e) {
      inform("Error fetching images: " + e.toString());
    }
    this.style.cursor = "";
  }

  async onsave(e) {
    this.style.cursor = "wait";
    try {
      let photos = new Array();
      for (let p of this.profile.photos) {
        if (p.deleted) continue;
        photos.push({
          url: p.url,
          nsfw: p.nsfw,
          caption: p.caption,
        });
      }
      let profile = {item: this.profile.item, photos: photos}
      let r = await fetch("/fotomat/update", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify(profile)
        });
      if (!r.ok) throw new Error("HTTP status " + r.status);
    } catch (e) {
      inform("Error updating profile images: " + e.toString());
    }
    this.style.cursor = "";
    this.mark_clean();
  }

  async oncopy(e) {
    if (!this.profile) return;
    console.log("gallery");
    let urls = new Array();
    for (let p of this.profile.photos) {
      if (p.deleted) continue;
      let url = p.url;
      if (p.nsfw) url = "!" + url;
      urls.push(url);
    }
    let gallery = `gallery: ${urls.join(" ")}`
    await navigator.clipboard.writeText(gallery);
    inform("Gallery URL coped to clipboard");
  }

  static stylesheet() {
    return `
      $ md-toolbar {
        padding: 10px;
      }
      $ md-input {
        width: 600px;
      }
      $ #title {
        padding-right: 16px;
      }
    `;
  }
}

Component.register(FotomatApp);

class PhotoProfile extends MdCard {
  visible() { return this.state?.photos?.length; }

  show_profile_gallery(current) {
    let photos = new Array();
    for (let p of this.state.photos) {
      if (p.deleted) continue;
      photos.push({
        url: p.url,
        nsfw: p.nsfw,
        text: p.caption,
        start: p.url == current});
    }

    let gallery = new PhotoGallery();
    gallery.bind(null, "nsfw", e => this.onnsfw(e, true));
    gallery.bind(null, "sfw", e => this.onnsfw(e, false));
    gallery.bind(null, "delimage", e => this.ondelimage(e));
    gallery.open(photos);
  }

  show_comparison(photo, dup) {
    let photos = new Array();
    photos.push({url: dup.url});
    photos.push({url: photo.url, nswf: photo.nsfw});

    let gallery = new PhotoGallery();
    gallery.open(photos);
  }

  mark_dirty() {
    this.match("#app").mark_dirty();
  }

  onnsfw(e, nsfw) {
    let url = e.detail;
    let strip = this.find_strip(url);
    if (strip) {
      strip.firstChild.state.nsfw = nsfw;
      strip.refresh();
      this.mark_dirty();
    }
  }

  ondelimage(e) {
    let url = e.detail;
    let strip = this.find_strip(url);
    if (strip) {
      strip.firstChild.state.deleted = true;
      strip.remove();
      this.mark_dirty();
    }
  }

  find_strip(url) {
    for (let e = this.firstChild; e; e = e.nextSibling) {
      if (e.firstChild?.state.url == url) return e;
    }
  }

  render() {
    if (!this.state) return;
    let onlydups = document.getElementById("onlydups").checked;
    let strips = new Array();
    for (let p of this.state.photos) {
      if (onlydups && !p.dups) continue;
      let strip = new Array();
      strip.push(p);
      if (p.dups) {
        for (let d of p.dups) {
          d.dupof = p;
          strip.push(d);
        }
      }
      strips.push(new PhotoStrip(strip));
    }
    if (strips.length == 0) {
      if (onlydups) {
        return `<p>no dups in ${this.state.photos.length} photos</p>`
      } else {
        return "<p>no photos found</p>"
      }
    } else {
      return strips;
    }
  }
}

Component.register(PhotoProfile);

class PhotoStrip extends Component {
  render() {
    let photos = new Array();
    for (let p of this.state) {
      photos.push(new PhotoBox(p));
    }
    return photos;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        flex-direction: row;
        gap: 16px;
      }
    `;
  }
}

Component.register(PhotoStrip);

class PhotoBox extends Component {
  onconnected() {
    this.attach(this.onclick, "click", "img");
  }

  onclick(e) {
    if (this.state.dupof) {
      this.match("photo-profile").show_comparison(this.state.dupof, this.state);
    } else {
      this.match("photo-profile").show_profile_gallery(this.state.url);
    }
  }

  render() {
    let photo = this.state;
    let url = imageurl(photo.url);
    let label = `${photo.width} x ${photo.height}`
    let h = new Array();

    h.push(`<img src="${url}" referrerpolicy="no-referrer">`);
    h.push("<div>");
    h.push(label);
    if (photo.nsfw) h.push(' <span class="nsfw">NSFW</span>');
    h.push("</div>");
    if (photo.item) {
      h.push(`<div><a href="https://ringgaard.com/kb/${photo.item}"
               target="_blank">${photo.item}</a>`);
    }

    return h.join("");
  }

  static stylesheet() {
    return `
      $ {
       padding: 10px;
      }
      $ img {
        height: 150px;
      }
      $ div {
        font-size: 14px;
      }
      $ .nsfw {
        border-radius: 3px;
        border: 1px solid;
        font-size: 12px;
        padding: 2px 4px;
        margin: 2px;
        color: #d10023;
      }

    `;
  }
}

Component.register(PhotoBox);

document.body.style = null;

// Knowledge base app.

import {Component} from "/common/lib/component.js";
import {MdCard, MdModal} from "/common/lib/material.js";

const mediadb = true;

var mobile_ckecked = false;
var is_mobile = false;
var allow_nsfw = false;

const photo_sources = {
  "upload.wikimedia.org": "wikimedia.org",
  "pbs.twimg.com": "twitter.com",
  "redd.it": "reddit.com",
  "pinimg.com": "pinterest.com",
  "live.staticflickr.com": "flickr.com",
  "ilarge.lisimg.com": "listal.com",
  "gstatic.com": "google.com",
}

function mod(m, n) {
  return ((m % n) + n) % n;
}

function isMobile() {
  if (mobile_ckecked) return is_mobile;
  let qs = new URLSearchParams(window.location.search);
  if (qs.get("mobile") == "1") {
    is_mobile = true;
  } else if (qs.get("desktop") == "1") {
    is_mobile = false;
  } else if (/iPhone|Android/i.test(navigator.userAgent)) {
    is_mobile = true;
  }
  mobile_ckecked = true;
  if (qs.get("nsfw") == "1") allow_nsfw = true;
  return is_mobile;
}

function wikiurl(id) {
  if (id.match(/^Q[0-9]+$/)) {
    return `https://www.wikidata.org/wiki/${id}`
  } else if (id.match(/^P[0-9]+$/)) {
    return `https://www.wikidata.org/wiki/Property:${id}`
  } else {
    return null;
  }
}

function imageurl(url, thumb) {
  if (mediadb) {
    let escaped = encodeURIComponent(url);
    escaped = escaped.replace(/%3A/g, ":").replace(/%2F/g, "/");
    return (thumb ? "/thumb/" : "/media/") + escaped;
  } else {
    return url;
  }
}

function filterGallery(gallery) {
  let filtered = [];
  let urls = new Set();
  for (let image of gallery) {
    if (!image.url) continue;
    if (!allow_nsfw && image.nsfw) continue;
    if (urls.has(image.url)) continue;
    filtered.push(image);
    urls.add(image.url);
  }
  return filtered;
}

//-----------------------------------------------------------------------------
// App
//-----------------------------------------------------------------------------

class KbApp extends Component {
  constructor() {
    super();
    this.pending = null;
  }

  onconnected() {
    if (isMobile()) this.find("#layout").update(".mobile");
    if (this.find("#websearch")) {
      this.bind("#websearch", "click", e => this.onwebsearch(e));
    }
    window.onpopstate = e => this.onpopstate(e);

    let path = window.location.pathname;
    if (path.startsWith("/kb/")) {
      let id = path.substring(4);
      if (id.length > 0) this.navigate(id);
    }
  }

  navigate(id) {
    let state = history.state;
    if (state) {
      let item = state.item;
      state.pos = this.find("md-content").scrollTop;
      history.replaceState(state, item.text, "/kb/" + item.ref);
    }

    fetch("/kb/item?fmt=cjson&id=" + encodeURIComponent(id))
      .then(response => response.json())
      .then((item) => {
        let state = {item: item, pos: 0};
        history.pushState(state, item.text, "/kb/" + item.ref);
        this.display(item);
        this.find("md-content").scrollTop = 0;
      })
      .catch(error => {
        console.log("Item error", id, error.message, error.stack);
      });
  }

  display(item) {
    this.find("#item").update(item);
    this.find("#document").update(item);
    this.find("#picture").update(filterGallery(item.gallery));
    this.find("#properties").update(item);
    this.find("#categories").update(item);
    this.find("#xrefs").update(item);
    window.document.title = item ? item.text : "SLING Knowledge base";
  }

  onpopstate(e) {
    let state = e.state;
    if (state) {
      this.display(state.item);
      this.find("md-content").scrollTop = state.pos;
    }
  }

  onwebsearch(e) {
    let query = this.find("#search").query();
    if (query && query.length > 0) {
      let url = "https://www.google.com/search?q=" + encodeURIComponent(query);
      window.open(url, "_blank", "noopener,noreferrer");
    }
  }
}

Component.register(KbApp);

//-----------------------------------------------------------------------------
// Link
//-----------------------------------------------------------------------------

class KbLink extends Component {
  onconnected() {
    this.bind(null, "click", e => this.onclick(e));
  }

  onclick(e) {
    if (e.ctrlKey) {
      window.open("/kb/" + this.props.ref, "_blank");
    } else {
      this.match("#app").navigate(this.props.ref);
    }
  }

  static stylesheet() {
    return `
      $ {
        color: #0b0080;
      }

      $:hover {
        cursor: pointer;
      }
    `;
  }
}

Component.register(KbLink);

//-----------------------------------------------------------------------------
// Search box
//-----------------------------------------------------------------------------

class KbSearchBox extends Component {
  onconnected() {
    this.bind("md-search", "query", e => this.onquery(e));
    this.bind("md-search", "item", e => this.onitem(e));
  }

  onquery(e) {
    let detail = e.detail
    let target = e.target;
    let params = "fmt=cjson";
    let query = detail.trim();
    if (query.endsWith(".")) {
      params += "&fullmatch=1";
      query = query.slice(0, -1);
    }

    let app = this.match("#app");
    fetch("/kb/query?" + params + "&q=" + encodeURIComponent(query))
      .then(response => response.json())
      .then((data) => {
        let items = [];
        for (let item of data.matches) {
          let elem = document.createElement("md-search-item");
          elem.setAttribute("name", item.text);
          elem.setAttribute("value", item.ref);

          let title = document.createElement("span");
          title.className = "item-title";
          title.appendChild(document.createTextNode(item.text));
          elem.appendChild(title);

          if (item.description) {
            let desciption = document.createElement("span");
            desciption.className = "item-description";
            desciption.appendChild(document.createTextNode(item.description));
            elem.appendChild(desciption);
          }

          items.push(elem);
        }
        target.populate(detail, items);
      })
      .catch(error => {
        console.log("Query error", query, error.message, error.stack);
      });
  }

  onitem(e) {
    let id = e.detail;
    this.match("#app").navigate(id);
  }

  query() {
    return this.find("md-search").query();
  }

  render() {
    return `
      <form>
        <md-search
          placeholder="Search knowledge base..."
          min-length=2
          autofocus>
        </md-search>
      </form>
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: block;
        width: 100%;
        max-width: 800px;
        padding-left: 10px;
        padding-right: 3px;
      }

      $ form {
        display: flex;
        width: 100%;
      }

      $ .item-title {
        font-weight: bold;
        display: block;
        padding: 2px 10px 2px 10px;
      }

      $ .item-description {
        display: block;
        padding: 0px 10px 0px 10px;
      }
    `;
  }
}

Component.register(KbSearchBox);

//-----------------------------------------------------------------------------
// Property table
//-----------------------------------------------------------------------------

class KbPropertyTable extends Component {
  render() {
    if (!this.state || this.state.length == 0) return "";
    let properties = this.state;
    let out = [];

    function propname(prop) {
      if (prop.ref) {
        out.push('<kb-link ref="');
        out.push(prop.ref);
        out.push('">');
      }
      out.push(Component.escape(prop.property));
      if (prop.ref) {
        out.push('</kb-link>');
      }
    }

    function propval(val) {
      if (val.ref) {
        out.push('<kb-link ref="');
        out.push(val.ref);
        out.push('">');
      } else if (val.url) {
        out.push('<a href="');
        out.push(val.url.replace(/"/g, '&quot;'));
        out.push('" target="_blank" rel="noreferrer" tabindex="-1">');
      }
      if (val.text) {
        out.push(Component.escape(val.text));
      } else {
        out.push('&#x1f6ab;');
      }
      if (val.ref) {
        out.push('</kb-link>');
      } else if (val.url) {
        out.push('</a>');
      }
      if (val.lang) {
        out.push(' <span class="prop-lang">[');
        out.push(Component.escape(val.lang));
        out.push(']</span>');
      }
    }

    for (const prop of properties) {
      out.push('<div class="prop-row">');

      // Property name.
      out.push('<div class="prop-name">');
      propname(prop);
      out.push('</div>');

      // Property value(s).
      out.push('<div class="prop-values">');
      for (const val of prop.values) {
        out.push('<div class="prop-value">');
        propval(val);
        out.push('</div>');

        // Property qualifiers.
        if (val.qualifiers) {
          out.push('<div class="qual-tab">');
          for (const qual of val.qualifiers) {
            out.push('<div class="qual-row">');

            // Qualifier name.
            out.push('<div class="qprop-name">');
            propname(qual);
            out.push('</div>');

            // Qualifier value(s).
            out.push('<div class="qprop-values">');
            for (const qval of qual.values) {
              out.push('<div class="qprop-value">');
              propval(qval);
              out.push('</div>');
            }
            out.push('</div>');

            out.push('</div>');
          }
          out.push('</div>');
        }
      }
      out.push('</div>');
      out.push('</div>');
    }

    return out.join("");
  }

  static stylesheet() {
    return `
      $ {
        display: table;
        font-size: 16px;
        border-collapse: collapse;
        width: 100%;
        table-layout: fixed;
      }

      $ .prop-row {
        display: table-row;
        border-top: 1px solid lightgrey;
      }

      $ .prop-row:first-child {
        display: table-row;
        border-top: none;
      }

      $ .prop-name {
        display: table-cell;
        font-weight: 500;
        width: 20%;
        padding: 8px;
        vertical-align: top;
        overflow-wrap: break-word;
      }

      $ .prop-values {
        display: table-cell;
        vertical-align: top;
        padding-bottom: 8px;
      }

      $ .prop-value {
        padding: 8px 8px 0px 8px;
        overflow-wrap: break-word;
      }

      $ .prop-lang {
        color: #808080;
        font-size: 13px;
      }

      $ .prop-value a {
        color: #0b0080;
        text-decoration: none;
        cursor: pointer;
        outline: none;
      }

      $ .qual-tab {
        display: table;
        border-collapse: collapse;
      }

      $ .qual-row {
        display: table-row;
      }

      $ .qprop-name {
        display: table-cell;
        font-size: 13px;
        vertical-align: top;
        padding: 1px 3px 1px 30px;
        width: 150px;
      }

      $ .qprop-values {
        display: table-cell;
        vertical-align: top;
      }

      $ .qprop-value {
        font-size: 13px;
        vertical-align: top;
        padding: 1px 1px 1px 1px;
      }

      $ .qprop-value a {
        color: #0b0080;
        text-decoration: none;
        cursor: pointer;
        outline: none;
      }

      .mobile $ {
        display: flex;
        flex-direction: column;
        font-size: 16px;
        border-collapse: collapse;
        width: 100%;
      }

      .mobile $ .prop-row {
        display: flex;
        flex-direction: column;
        border-top: 1px solid lightgrey;
      }

      .mobile $ .prop-row:first-child {
        display: flex;
        flex-direction: column;
        border-top: none;
      }

      .mobile $ .prop-name {
        display: block;
        font-weight: 300;
        font-size: 14px;
        width: auto;
        padding: 1px;
        vertical-align: top;
      }

      .mobile $ .prop-value:first-child {
        padding: 2px 8px 0px 8px;
      }
    `;
  }
}

Component.register(KbPropertyTable);

//-----------------------------------------------------------------------------
// Item list
//-----------------------------------------------------------------------------

class KbItemList extends Component {
  render() {
    if (!this.state || this.state.length == 0) return "";
    let items = this.state;
    let out = [];

    for (const item of items) {
      out.push('<div class="row">');
      if (item.ref) {
        out.push('<kb-link ref="');
        out.push(item.ref);
        out.push('">');
      }
      if (item.text) {
        let text = item.text.substr(item.text.indexOf(':') + 1);
        out.push(Component.escape(text));
      } else {
        out.push('???');
      }
      if (item.ref) {
        out.push('</kb-link>');
      }
      out.push('</div>');
    }

    return out.join("");
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        flex-direction: column;
        font-size: 16px;
      }

      $ .row {
        border-top: 1px solid lightgrey;
        padding: 8px;
      }

      $ .row:first-child {
        border-top: none;
        padding: 8px;
      }
    `;
  }
}

Component.register(KbItemList);

//-----------------------------------------------------------------------------
// Item card
//-----------------------------------------------------------------------------

class KbItemCard extends MdCard {
  onconnected() {
    if (this.find("#code")) {
      this.bind("#code", "click", e => this.oncode(e));
    }
    if (this.find("#copy")) {
      this.bind("#copy", "click", e => this.oncopy(e));
    }
  }

  visible() {
    let item = this.state;
    return item;
  }

  onupdate() {
    let item = this.state;
    this.find("#title").update(item.text);
    this.find("#ref").update({
      url: wikiurl(item.ref),
      text: item.ref,
      external: true,
    });
    this.find("#description").update(item.description);
    this.find("#datatype").update(item.type ? "Datatype: " + item.type : "");
  }

  oncode(e) {
    let item = this.state;
    let url = "/kb/frame?fmt=txt&id=" + encodeURIComponent(item.ref);
    window.open(url, "_blank");
  }

  oncopy(e) {
    let item = this.state;
    let ref = this.find("#ref");
    const selection = window.getSelection();
    selection.removeAllRanges();
    const range = document.createRange();
    range.selectNodeContents(ref);
    selection.addRange(range);
    document.execCommand("copy");
  }

  static stylesheet() {
    return MdCard.stylesheet() + `
      $ #title {
        display: block;
        font-size: 28px;
      }

      $ #ref a {
        display: block;
        font-size: 13px;
        color: #808080;
        text-decoration: none;
        width: fit-content;
        outline: none;
      }

      $ #description {
        font-size: 16px;
      }

      $ #datatype {
        font-size: 16px;
      }
    `;
  }
}

Component.register(KbItemCard);

//-----------------------------------------------------------------------------
// Document card
//-----------------------------------------------------------------------------

class KbDocumentCard extends MdCard {
  onconnected() {
    this.bind(null, "click", e => this.onclick(e));
  }

  onclick(e) {
    let ref = e.target.getAttribute("ref");
    if (ref) {
      if (e.ctrlKey) {
        window.open("/kb/" + ref, "_blank");
      } else {
        this.match("#app").navigate(ref);
      }
    }
  }

  visible() {
    let item = this.state;
    return item && item.document;
  }

  onupdate() {
    let item = this.state;
    this.find("#url").update({
      url: item.url,
      text: item.url + ":",
      external: true,
    });
    this.find("#text").innerHTML = item.document;
  }

  static stylesheet() {
    return `
      @import url('https://fonts.googleapis.com/css?family=Lato|Lora:400,400i,700,700i');

      ${MdCard.stylesheet()}

      $ #url {
        display: block;
        overflow: hidden;
        text-overflow: ellipsis;
      }
      $ #url a {
        font-size: 13px;
        color: #808080;
        text-decoration: none;
        outline: none;
      }
      $ #text {
        font: 16px lora, georgia, serif;
        line-height: 1.4;
      }
      $ #text a[ref] {
        color: #0b0080;
        text-decoration: none;
        cursor: pointer;
        outline: none;
      }
      $ #text a[ref]:hover{
        text-decoration: underline;
      }
    `;
  }
}

Component.register(KbDocumentCard);

//-----------------------------------------------------------------------------
// Property card
//-----------------------------------------------------------------------------

class KbPropertyCard extends MdCard {
  visible() {
    let item = this.state;
    return item && item.properties && item.properties.length > 0;
  }

  onupdate() {
    let item = this.state;
    this.find("#property-table").update(item.properties);
  }

  static stylesheet() { return MdCard.stylesheet(); }
}

Component.register(KbPropertyCard);

//-----------------------------------------------------------------------------
// Category card
//-----------------------------------------------------------------------------

class KbCategoryCard extends MdCard {
  visible() {
    let item = this.state;
    return item && item.categories && item.categories.length > 0;
  }

  onupdate() {
    let item = this.state;
    let categories = item.categories;
    this.find("#category-list").update(categories);
  }

  static stylesheet() { return MdCard.stylesheet(); }
}

Component.register(KbCategoryCard);

//-----------------------------------------------------------------------------
// Picture card
//-----------------------------------------------------------------------------

class KbPictureCard extends MdCard {
  onconnected() {
    this.bind(null, "click", e => this.onopen(e));
  }

  onupdated() {
    let images = this.state;
    if (images && images.length > 0) {
      let image = images[0];
      let caption = image.text;
      if (caption) {
        caption = caption.replace(/\[\[|\]\]/g, '');
      }
      if (images.length > 1) {
        if (!caption) caption = "";
        caption += ` [1/${images.length}]`;
      }

      this.find(".photo").update(imageurl(image.url, true));
      this.find(".caption").update(caption);
    } else {
      this.find(".photo").update(null);
      this.find(".caption").update(null);
    }
  }

  visible() {
    return this.state && this.state.length > 0;
  }

  onopen(e) {
    let modal = new KbLightbox();
    modal.open(this.state);
  }

  static stylesheet() {
    return MdCard.stylesheet() + `
      $ {
        text-align: center;
        cursor: pointer;
      }

      $ .caption {
        display: block;
        font-size: 13px;
        color: #808080;
        padding: 5px;
      }

      $ img {
        max-width: 100%;
        max-height: 480px;
        vertical-align: middle
      }
    `;
  }
}

Component.register(KbPictureCard);

//-----------------------------------------------------------------------------
// Xref card
//-----------------------------------------------------------------------------

class KbXrefCard extends MdCard {
  visible() {
    let item = this.state;
    return item && item.xrefs && item.xrefs.length;
  }

  onupdate() {
    let item = this.state;
    this.find("#xref-table").update(item.xrefs);
  }

  static stylesheet() {
    return MdCard.stylesheet() + `
      $ .prop-name {
        font-size: 13px;
        font-weight: normal;
        width: 40%;
        padding: 8px;
        vertical-align: top;
      }

      $ .prop-values {
        font-size: 13px;
        max-width: 0;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
      }

      $ .qprop-name {
        font-size: 11px;
        vertical-align: top;
        padding: 1px 3px 1px 20px;
        width: 100px;
      }

      $ .qprop-value {
        font-size: 11px;
        padding: 1px 1px 1px 1px;
      }

      .mobile $ .prop-name {
        width: auto;
        padding: 1px;
      }

      .mobile $ .prop-values {
        font-size: inherit;
        max-width: none;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
      }

      .mobile $ .prop-value:first-child {
        padding: 2px 8px 0px 8px;
      }
    `;
  }
}

Component.register(KbXrefCard);

//-----------------------------------------------------------------------------
// Lightbox
//-----------------------------------------------------------------------------

class KbLightbox extends MdModal {
  onconnected() {
    this.bind(".image", "click", e => this.onclick(e));
    this.bind(".prev", "click", e => this.onprev(e));
    this.bind(".next", "click", e => this.onnext(e));
    this.bind(".close", "click", e => this.close());
    this.bind(null, "keydown", e => this.onkeypress(e));
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
      this.close();
    }
    this.focus();
  }

  onload(e) {
    let image = e.target;
    image.style.cursor = null;
    let n = image.serial;
    let photo = this.photos[n];
    photo.width = image.naturalWidth;
    photo.height = image.naturalHeight;
    if (n == this.current) {
      let w = photo.width;
      let h = photo.height;
      this.find(".size").update(w && h ? `${w} x ${h}`: null);
    }
  }

  onclick(e) {
    let url = imageurl(this.photos[this.current].url, false);
    window.open(url, "_blank", "noopener,noreferrer");
  }

  onprev(e) {
    this.move(-this.stepsize(e));
  }

  onnext(e) {
    this.move(this.stepsize(e));
  }

  stepsize(e) {
    if (e.shiftKey) return 10;
    if (e.ctrlKey) return 30;
    if (e.altKey) return 100;
    return 1;
  }

  move(n) {
    let size = this.photos.length;
    this.current = mod(this.current + n, size);
    if (size > 0) {
      this.preload(this.current, n);
      this.display(this.current);
    }
  }

  display(n) {
    let photo = this.photos[n];
    let caption = photo.caption;
    if (caption) {
      caption = caption.replace(/\[\[|\]\]/g, "");
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

    this.find(".domain").update({url: photo.url, text: domain});
    this.find(".nsfw").update(photo.nsfw ? "NSFW" : null);
    this.find(".copyright").update(copyrighted);
    if (photo.width && photo.height) {
      let w = photo.width;
      let h = photo.height;
      this.find(".size").update(w && h ? `${w} x ${h}`: null);
    }
  }

  preload(position, direction) {
    for (var i = 0; i < 5; ++i) {
      let n = mod(position + i * direction, this.photos.length);
      if (this.photos[n].image == null) {
        var image = new Image();
        image.src = imageurl(this.photos[n].url, false);
        image.classList.add("image");
        image.referrerPolicy = "no-referrer";
        image.addEventListener("load", e => this.onload(e));
        image.serial = n;
        image.style.cursor = "wait";
        if (this.photos[n].url.endsWith(".svg")) {
          image.style.background = "white";
          image.style.padding = "10px";
        }
        this.photos[n].image = image;
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
          <md-icon-button class="close" icon="close"></md-icon-button>

          <div class="source">
            <md-link class="domain" newtab="1" external="1"></md-link>
            <kb-copyright class="copyright"></kb-copyright>
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
    return MdModal.stylesheet() + `
      $ {
        background-color: rgba(0, 0, 0, 0.9);
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
      }

      $ .counter {
        position: absolute;
        top: 0;
        left: 0;

        color: rgb(255, 255, 255);
        mix-blend-mode: difference;
        font-size: 12px;
        padding: 8px 12px;
      }

      $ .close i {
        position: absolute;
        top: 0;
        right: 0;

        color: white;
        padding: 16px;
        font-size: 24px;
        font-weight: bold;
      }

      $ .source {
        position: absolute;
        bottom: 0;
        left: 0;

        color: rgb(255, 255, 255);
        mix-blend-mode: difference;
        font-size: 12px;
        padding: 8px 12px;
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
        mix-blend-mode: difference;
        font-size: 12px;
        padding: 8px 12px;
      }

      $ .close:hover, $ .close:focus {
        color: #999;
        text-decoration: none;
        cursor: pointer;
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

Component.register(KbLightbox);

//-----------------------------------------------------------------------------
// Copyright notice
//-----------------------------------------------------------------------------

class KbCopyright extends Component {
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
        display: inline-block;
        vertical-align: middle;
        font-size: 14px;
        cursor: default;
      }
    `;
  }
}

Component.register(KbCopyright);

//-----------------------------------------------------------------------------
// HTML template
//-----------------------------------------------------------------------------

document.body.innerHTML = `
<kb-app id="app">
  <one-of id="layout" selected=".desktop">

    <!------------------------------------------------------------------->
    <!-- Desktop layout                                                -->
    <!------------------------------------------------------------------->

    <md-column-layout class="desktop">
      <md-toolbar>
        <md-toolbar-logo></md-toolbar-logo>
        <div>Knowledge</div>
        <kb-search-box id="search"></kb-search-box>
        <md-icon-button id="websearch" icon="search"></md-icon-button>
      </md-toolbar>

      <md-content>
        <md-row-layout>
          <md-column-layout style="flex: 1 1 66%;">

            <!-- Item card -->
            <kb-item-card id="item">
              <md-card-toolbar>
                <div>
                  <md-text id="title"></md-text>
                  <md-link id="ref" notab="1" newtab="1" external="1"></md-link>
                </div>
                <md-spacer></md-spacer>
                <md-icon-button id="code" icon="code"></md-icon-button>
                <md-icon-button id="copy" icon="content_copy"></md-icon-button>
              </md-card-toolbar>
              <div><md-text id="description"></md-text></div>
              <div><md-text id="datatype"></md-text></div>
            </kb-item-card>

            <!-- Document card -->
            <kb-document-card id="document">
              <md-link id="url" notab="1" newtab="1" external="1"></md-link>
              <div id="text"></div>
            </kb-document-card>

            <!-- Item properties card -->
            <kb-property-card id="properties">
              <kb-property-table id="property-table">
              </kb-property-table>
            </kb-property-card>

          </md-column-layout>

          <md-column-layout style="flex: 1 1 33%;">

            <!-- Item picture card -->
            <kb-picture-card id="picture">
              <md-image class="photo"></md-image>
              <md-text class="caption"></md-text>
            </kb-picture-card>

            <!-- Item references card -->
            <kb-xref-card id="xrefs">
              <md-card-toolbar>
                <div>References</div>
              </md-card-toolbar>
              <kb-property-table id="xref-table">
              </kb-property-table>
            </kb-xref-card>

            <!-- Item categories card -->
            <kb-category-card id="categories">
              <md-card-toolbar>
                <div>Categories</div>
              </md-card-toolbar>

              <kb-item-list id="category-list">
              </kb-item-list>
            </kb-category-card>
          </md-column-layout>

        </md-row-layout>
      </md-content>
    </md-column-layout>

    <!------------------------------------------------------------------->
    <!-- Mobile layout                                                 -->
    <!------------------------------------------------------------------->

    <md-column-layout class="mobile">
      <md-toolbar>
        <md-toolbar-logo></md-toolbar-logo>
        <kb-search-box id="search"></kb-search-box>
      </md-toolbar>

      <md-content>
        <md-column-layout>

          <!-- Item card -->
          <kb-item-card id="item">
            <md-card-toolbar>
              <div>
                <md-text id="title"></md-text>
                <md-link id="ref" notab="1" newtab="1" external="1"></md-link>
              </div>
            </md-card-toolbar>
            <div><md-text id="description"></md-text></div>
            <div><md-text id="datatype"></md-text></div>
          </kb-item-card>

          <!-- Document card -->
          <kb-document-card id="document">
            <md-link id="url" notab="1" newtab="1" external="1"></md-link>
            <div id="text"></div>
          </kb-document-card>

          <!-- Item picture card -->
          <kb-picture-card id="picture">
            <md-image class="photo"></md-image>
            <md-text class="caption"></md-text>
          </kb-picture-card>

          <!-- Item properties card -->
          <kb-property-card id="properties">
            <kb-property-table id="property-table">
            </kb-property-table>
          </kb-property-card>

          <!-- Item references card -->
          <kb-xref-card id="xrefs">
            <md-card-toolbar>
              <div>References</div>
            </md-card-toolbar>
            <kb-property-table id="xref-table">
            </kb-property-table>
          </kb-xref-card>

          <!-- Item categories card -->
          <kb-category-card id="categories">
            <md-card-toolbar>
              <div>Categories</div>
            </md-card-toolbar>

            <kb-item-list id="category-list">
            </kb-item-list>
          </kb-category-card>

        </md-column-layout>
      </md-content>
    </md-column-layout>
  </one-of>
</kb-app>
`;


// Knowledge base app.

import {Component} from "/common/lib/component.js";
import {MdCard} from "/common/lib/material.js";

const mediadb = true;

var mobile_ckecked = false;
var is_mobile = false;
var allow_nsfw = false;

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

function imageurl(image) {
  if (mediadb) {
    return "/media/" + encodeURIComponent(image.url);
  } else {
    return image.url;
  }
}

function filterGallery(gallery) {
  if (allow_nsfw) return gallery;
  let filtered = [];
  for (let image of gallery) {
    if (image.nsfw) continue;
    filtered.push(image);
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
    let id = path.substring(path.indexOf('/', 1) + 1);
    if (id.length > 0) {
      this.navigate(id);
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
        console.log("Item error", id, error.message);
      });
  }

  display(item) {
    this.find("#item").update(item);
    this.find("#document").update(item);
    this.find("#properties").update(item);
    this.find("#categories").update(item);
    this.find("#xrefs").update(item);
    if (this.find("#gallery")) {
      this.find("#gallery").update(item["gallery"]);
    }
    if (this.find("#picturex")) {
      this.find("#picturex").update(item);
    }
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
      window.open(url,'_blank');
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
      window.open("/kb/" + this.props.ref, '_blank');
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
        console.log("Query error", query, error.message);
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
        outline: none
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
        outline: none
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
    window.open(url,'_blank');
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
        window.open("/kb/" + ref, '_blank');
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
        outline: none
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

const commons_url = "https://commons.wikimedia.org/wiki/File:";

class KbPictureCard extends MdCard {
  visible() {
    let item = this.state;
    return item && item.thumbnail;
  }

  render() {
    let url = this.state.thumbnail.replace(/"/g, '&quot;');
    if (mediadb) url = "/media/" + url;
    return `<a href="${url}" target="_blank" rel="noreferrer" tabindex="-1">
              <img src="${url}" rel="noreferrer">
            </a>`;
  }

  static stylesheet() {
    return MdCard.stylesheet() + `
      $ {
        text-align: center;
      }

      $ a {
        outline: none;
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
// Gallery card
//-----------------------------------------------------------------------------

class KbGalleryCard extends MdCard {
  onconnected() {
    this.bind(".photo", "click", e => this.onopen(e));
    this.bind(".left", "click", e => this.onbackward(e));
    this.bind(".right", "click", e =>this.onforward(e));
    this.bind(null, "keydown", e =>this.onkeypress(e));
  }

  onupdate() {
    this.images = filterGallery(this.state);
    this.current = 0;
    let navigate = this.images.length > 1;
    this.find(".left").style.visibility = navigate ? "" : "hidden";
    this.find(".right").style.visibility = navigate ? "" : "hidden";
    this.display(this.images[this.current]);
  }

  onkeypress(e) {
    if (e.keyCode == 37) {
      this.onbackward(e);
    } else if (e.keyCode == 39) {
      this.onforward(e);
    } else if (e.keyCode == 13) {
      this.onopen(e);
    }
  }

  onbackward(e) {
    this.focus();
    if (this.current > 0) {
      this.current -= 1;
      this.display(this.images[this.current]);
    }
  }

  onforward(e) {
    this.focus();
    if (this.current < this.images.length - 1) {
      this.current += 1;
      this.display(this.images[this.current]);
    }
  }

  onopen(e) {
    let modal = document.getElementById("lightbox");
    modal.open({images: this.state, current: this.current});
  }

  visible() {
    return this.images && this.images.length > 0;
  }

  display(image) {
    if (image) {
      let caption = image.text;
      if (caption) {
        caption = caption.replace(/\[\[|\]\]/g, '');
      }
      if (this.images.length > 1) {
        if (!caption) caption = "";
        caption += ` [${this.current + 1}/${this.images.length}]`;
      }
      this.find(".photo").update(imageurl(image));
      this.find("#caption").update(caption);
    } else {
      this.find(".photo").update(null);
      this.find("#caption").update(null);
    }
  }

  static stylesheet() {
    return MdCard.stylesheet() + `
      $ {
        padding: 15px 2px 15px 2px;
      }

      $ #picture {
        outline: none;
        display: flex;
        flex-direction: row;
      }

      $ #photo-box {
        width:auto;
        height:auto;
        margin-left: auto;
        margin-right: auto;
      }

      $ .photo img {
        cursor: pointer;
        max-width: 400px;
        max-height: 480px;
        width:auto;
        height:auto;
        display: block;
        margin-left: auto;
        margin-right: auto;
      }

      $ .left, .right {
        flex: 0 0 13px;
        cursor: pointer;
      }

      $ .left:hover, .right:hover {
        background-color: rgba(0,0,0,0.1);
      }

      $ md-icon {
        color: white;
      }

      $ #caption {
        display: block;
        font-size: 13px;
        text-align: center;
        color: #808080;
        padding: 5px;
      }
    `;
  }
}

Component.register(KbGalleryCard);

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

class KbLightbox extends Component {
  onconnected() {
    this.bind(".photo", "click", e => this.onopen(e));
    this.bind(".prev", "click", e => this.onprev(e));
    this.bind(".next", "click", e => this.onnext(e));
    this.bind(".close", "click", e => this.close());
    this.bind(null, "keydown", e => this.onkeypress(e));
  }

  onupdate() {
    this.images = filterGallery(this.state.images);
    this.current = mod(this.state.current, this.images.length)
    this.cache = Array(this.images).fill(null);
    this.display(this.images[this.current]);
    this.preload(this.current, 1);
  }

  onkeypress(e) {
    if (e.keyCode == 37) {
      this.onprev(e);
    } else if (e.keyCode == 39) {
      this.onnext(e);
    } else if (e.keyCode == 27) {
      this.close();
    }
  }

  onprev(e) {
    this.move(-this.stepsize(e));
  }

  onnext(e) {
    this.move(this.stepsize(e));
  }

  onopen(e) {
    let url = imageurl(this.images[this.current]);
    window.open(url,'_blank');
  }

  stepsize(e) {
    if (e.shiftKey) return 10;
    if (e.ctrlKey) return 30;
    if (e.altKey) return 100;
    return 1;
  }

  move(n) {
    let size = this.images.length;
    this.current = mod(this.current + n, size);
    this.display(this.images[this.current]);
    this.preload(this.current, n);
  }

  open(state) {
    this.update(state);
    this.style.display = "block";
    this.focus();
  }

  close() {
    this.style.display = "none";
    this.cache = null;
  }

  display(image) {
    if (image) {
      let caption = image.text;
      if (caption) {
        caption = caption.replace(/\[\[|\]\]/g, '');
      }
      this.find(".photo").src = imageurl(image);
      this.find(".caption").update(caption);
      let counter = `${this.current + 1} / ${this.images.length}`;
      this.find(".counter").update(counter);
    } else {
      this.find(".photo").src = null;
      this.find(".caption").update(null);
    }
  }

  preload(position, direction) {
    for (var i = 1; i < 5; ++i) {
      let n = mod(position + i * direction, this.images.length);
      if (this.cache[n] == null) {
        console.log("preload", n, imageurl(this.images[n]));
        var image = new Image();
        image.src = imageurl(this.images[n]);
        this.cache[n] = image;
      }
    }
  }

  static stylesheet() {
    return `
      $ {
        display: none;
        position: fixed;
        z-index: 100;
        padding-top: 60px;
        left: 0;
        top: 0;
        width: 100%;
        height: 100%;
        background-color: rgba(0, 0, 0, 0.9);
      }

      $ .close {
        color: white;
        position: absolute;
        top: 10px;
        right: 25px;
        font-size: 35px;
        font-weight: bold;
      }

      $ .close:hover, $ .close:focus {
        color: #999;
        text-decoration: none;
        cursor: pointer;
      }

      $ .content {
        position: relative;
        margin: auto;
        padding: 0;
        width: 100%;
        height: 100%;
      }

      $ .prev, $ .next {
        cursor: pointer;
        position: absolute;
        top: 50%;
        width: auto;
        padding: 16px;
        margin-top: -50px;
        color: white;
        font-weight: bold;
        font-size: 20px;
        transition: 0.6s ease;
        user-select: none;
      }

      $ .next {
        right: 0;
      }

      $ .prev:hover, $ .next:hover {
        background-color: rgba(0, 0, 0, 0.8);
      }

      $ .counter {
        color: rgb(255, 255, 255);
        mix-blend-mode: difference;
        font-size: 12px;
        padding: 8px 12px;
        position: absolute;
        top: 0;
      }

      $ .image {
        height: 85%;
        margin: auto;
      }

      $ .photo {
        display: block;
        max-width: 100%;
        max-height: 100%;
        margin: auto;
      }

      $ .legend {
        text-align: center;
        color: white;
        padding: 5px;
      }
    `;
  }
}

Component.register(KbLightbox);


// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Knowledge base browser.

import {Component, stylesheet} from "/common/lib/component.js";
import {MdApp, MdCard, MdModal, MdSearchResult, StdDialog}
  from "/common/lib/material.js";
import {PhotoGallery, imageurl, censor} from "/common/lib/gallery.js";

var settings = JSON.parse(window.localStorage.getItem("settings") || "{}");

var mobile_ckecked = false;
var is_mobile = false;

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
  if (qs.get("nsfw") == "1") settings.nsfw = true;
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

stylesheet(`
@import url('/common/font/lora.css');
@import url('/common/font/lato.css');
`);

//-----------------------------------------------------------------------------
// App
//-----------------------------------------------------------------------------

class KbApp extends MdApp {
  onconnected() {
    if (this.find("#websearch")) {
      this.bind("#websearch", "click", e => this.onwebsearch(e));
    }
    window.onkeydown = e => this.onkeypress(e);
    window.onpopstate = e => this.onpopstate(e);

    let itemid = document.head.querySelector('meta[property="itemid"]');
    if (itemid) {
      let id = itemid.content;
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
    this.find("#picture").update({
      itemid: item.ref,
      images: censor(item.gallery, settings.nsfw),
    });
    this.find("#properties").update(item);
    this.find("#categories").update(item);
    this.find("#xrefs").update(item);
    window.document.title = item ? item.text : "KnolBase";
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

  onkeypress(e) {
    if (e.keyCode == 27) {
      this.find("#search").clear();
    } else if (e.keyCode == 67 && e.ctrlKey) {
      if (window.getSelection().type != "Range") {
        this.find("#item").copy();
      }
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
      window.open("/kb/" + this.attrs.ref, "_blank");
    } else {
      this.match("#app").navigate(this.attrs.ref);
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
    let path = "/kb/query";
    let params = "fmt=cjson";
    let query = detail.trim();
    let search = false;
    if (query.endsWith(".")) {
      params += "&fullmatch=1";
      query = query.slice(0, -1);
    } else  if (query.endsWith("?")) {
      path = "/kb/search";
      query = query.slice(0, -1);
      search = true;
    }

    fetch(`${path}?${params}&q=${encodeURIComponent(query)}`)
      .then(response => response.json())
      .then((data) => {
        let items = [];
        for (let item of data.matches) {
          items.push(new MdSearchResult({
            ref: item.ref,
            name: item.text,
            description: item.description,
          }));
        }
        if (!search) {
          items.push(new MdSearchResult({
            name: "more...",
            description: 'search for "' + query + '" 🔎',
            query: query,
          }));
        }
        target.populate(detail, items);
      })
      .catch(error => {
        console.log("Query error", query, error.message, error.stack);
      });
  }

  onitem(e) {
    let item = e.detail;
    if (item.query) {
      let search = this.find("md-search");
      let query = item.query + "?";
      search.set(query);
      this.onquery({detail: query, target: search});
    } else {
      this.match("#app").navigate(item.ref);
    }
  }

  query() {
    return this.find("md-search").query();
  }

  clear() {
    return this.find("md-search").clear();
  }

  render() {
    return `
      <md-search
        placeholder="Search knowledge base..."
        min-length=2
        autoselect=1
        autofocus>
      </md-search>
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        width: 100%;
        max-width: 800px;
        padding-left: 10px;
        padding-right: 3px;
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
      if (val.age) {
        out.push(' <span class="prop-age">(');
        out.push(val.age);
        out.push(' yo)</span>');
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

      $ .prop-age {
        color: #808080;
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
    if (this.find("#imgsearch")) {
      this.bind("#imgsearch", "click", e => this.onimgsearch(e));
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

  onimgsearch(e) {
    let item = this.state;
    let url = `/photosearch?q="${encodeURIComponent(item.text)}"`;
    if (settings.nsfw) url += "&nsfw=1";
    window.open(url, "_blank");
  }

  copy() {
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
    return `
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
    let images = this.state.images;
    if (images && images.length > 0) {
      let index = 0;
      for (let i = 0; i < images.length; ++i) {
        if (!images[i].nsfw) {
          index = i;
          break;
        }
      }
      let image = images[index];
      let caption = image.text;
      if (caption) {
        caption = caption.replace(/\[\[|\]\]/g, '');
      }
      if (images.length > 1) {
        if (!caption) caption = "";
        caption += ` [${index + 1}/${images.length}]`;
      }

      this.find(".photo").update(imageurl(image.url, true));
      this.find(".caption").update(caption);
    } else {
      this.find(".photo").update(null);
      this.find(".caption").update(null);
    }
  }

  visible() {
    return this.state && this.state.images.length > 0;
  }

  onopen(e) {
    this.edits = [];
    let modal = new PhotoGallery();
    for (let event of ["nsfw", "sfw", "delimage"]) {
      modal.bind(null, event, e => {
        let url = e.detail;
        this.edits.push({event, url});
      });
    }
    modal.bind(null, "picedit", e => this.onpicedit());
    modal.open(this.state.images);
  }

  async onpicedit() {
    let edits = this.edits;
    this.edits = undefined;

    let ok = await StdDialog.ask(
      "Edit photo profile",
      "Request update of the photo gallery for item?");
    if (!ok) return;

    let itemid = this.state.itemid;
    fetch("/redreport/picedit", {
      method: "POST",
      body: JSON.stringify({itemid, edits}),
    });
  }

  static stylesheet() {
    return `
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
    return `
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
// Desktop HTML template
//-----------------------------------------------------------------------------

const desktop_template = `
<kb-app id="app" class="desktop">
  <md-toolbar>
    <md-toolbar-logo></md-toolbar-logo>
    <div>KnolBase</div>
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
            <md-icon-button id="imgsearch" icon="image_search">
            </md-icon-button>
            <md-icon-button id="code" icon="code">
            </md-icon-button>
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
</kb-app>`;

//-----------------------------------------------------------------------------
// Mobile HTML template
//-----------------------------------------------------------------------------

const mobile_template = `
<kb-app id="app" class="mobile">
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
</kb-app>
`;

document.body.innerHTML = isMobile() ? mobile_template : desktop_template;


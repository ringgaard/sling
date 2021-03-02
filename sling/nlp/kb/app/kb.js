// Knowledge base app.

import {Component} from "/common/lib/component.js";
import {MdCard} from "/common/lib/material.js";

const mediadb = true;

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
  return is_mobile;
}

function wikiurl(id) {
  if (id[0] == "Q") {
    return `https://www.wikidata.org/wiki/${id}`
  } else if (id[0] == "P") {
    return `https://www.wikidata.org/wiki/Property:${id}`
  } else {
    return null;
  }
}

class KbApp extends Component {
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
      });
  }

  display(item) {
    this.find("#item").update(item);
    this.find("#properties").update(item);
    this.find("#categories").update(item);
    this.find("#xrefs").update(item);
    this.find("#picture").update(item);
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

class KbSearchBox extends Component {
  onconnected() {
    this.bind("md-search", "query", e => this.onquery(e));
    this.bind("md-search", "item", e => this.onitem(e));
  }

  onquery(e) {
    let params = "fmt=cjson";
    let query = e.detail.trimStart();
    if (query.endsWith(".")) {
      params += "&fullmatch=1";
      query = query.slice(0, -1);
    }
    let target = e.target;
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
        target.populate(items);
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
        padding: 3px 10px 0px 10px;
      }

      $ .item-description {
        display: block;
        padding: 0px 10px 0px 10px;
      }
    `;
  }
}

Component.register(KbSearchBox);

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
        font-weight: bold;
        width: 20%;
        padding: 8px;
        vertical-align: top;
      }

      $ .prop-values {
        display: table-cell;
        vertical-align: top;
        padding-bottom: 8px;
      }

      $ .prop-value {
        padding: 8px 8px 0px 8px;
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


// Knowledge base app.

import {Component} from "/common/lib/component.js";
import {MdCard} from "/common/lib/material.js";

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
  constructor() {
    super();
    this.item = null;
  }

  onconnected() {
    window.onpopstate = e => this.onpopstate(e);

    let path = window.location.pathname;
    let id = path.substring(path.indexOf('/', 1) + 1);
    if (id.length > 0) {
      this.navigate(id);
    }
  }

  navigate(id) {
    fetch("/kb/item?fmt=cjson&id=" + encodeURIComponent(id))
      .then(response => response.json())
      .then((item) => {
        history.pushState(item, item.text, "/kb/" + item.ref);
        this.display(item);
      });
  }

  display(item) {
    this.find("md-content").scrollTop = 0;
    this.find("#item").update(item);
    this.find("#properties").update(item);
    this.find("#categories").update(item);
    this.find("#xrefs").update(item);
    this.find("#picture").update(item);
    window.document.title = item ? item.text : "SLING Knowledge base";
  }

  onpopstate(e) {
    this.display(e.state);
  }
}

Component.register(KbApp);

class KbLink extends Component {
  onconnected() {
    this.bind(null, "click", e => this.onclick(e));
  }

  onclick(e) {
    this.match("#app").navigate(this.props.ref);
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
  constructor() {
    super();
    this.populating = false;
  }

  onconnected() {
    this.bind("md-search", "query", e => this.onquery(e));
    this.bind("md-search", "item", e => this.onitem(e));
  }

  onquery(e) {
    if (this.populating) return;
    this.populating = true;

    let query = e.detail;
    let target = e.target;
    fetch("/kb/query?fmt=cjson&q=" + encodeURIComponent(query))
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
        this.populating = false;
      });
  }

  onitem(e) {
    let id = e.detail;
    this.match("#app").navigate(id);
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
        width: 800px;
        padding-left: 10px;
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
        out.push(val.url);
        out.push('" target="_blank">');
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
        out.push(' <span class="prop-lang">(');
        out.push(Component.escape(val.lang));
        out.push(')</span>');
      }
    }

    out.push('<table class="prop-tab">');
    for (const prop of properties) {
      out.push('<tr class="prop-row">');

      // Property name.
      out.push('<td class="prop-name">');
      propname(prop);
      out.push('</td>');

      // Property value(s).
      out.push('<td class="prop-values">');
      for (const val of prop.values) {
        out.push('<div class="prop-value">');
        propval(val);
        out.push('</div>');

        // Property qualifiers.
        if (val.qualifiers) {
          out.push('<table class="qual-tab">');
          for (const qual of val.qualifiers) {
            out.push('<tr class="qual-row">');

            // Qualifier name.
            out.push('<td class="qprop-name">');
            propname(qual);
            out.push('</td>');

            // Qualifier value(s).
            out.push('<td class="qprop-values">');
            for (const qval of qual.values) {
              out.push('<div class="qprop-value">');
              propval(qval);
              out.push('</div>');
            }
            out.push('</td>');

            out.push('</tr>');
          }
          out.push('</table>');
        }


      }
      out.push('</td>');
    }
    out.push('</table>');

    return out.join("");
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        font-size: 16px;
      }

      $ .prop-tab {
        border-collapse: collapse;
        width: 100%;
      }

      $ .prop-row {
        border-top: 1px solid lightgrey;
      }

      $ .prop-name {
        font-weight: bold;
        width: 20%;
        padding: 8px;
        vertical-align: top;
      }

      $ .prop-name a {
        color: #0b0080;
        text-decoration: none;
        cursor: pointer;
      }

      $ .prop-values {
        vertical-align: top;
      }

      $ .prop-value {
        padding: 8px;
      }

      $ .prop-lang {
        color: #808080;
        font-size: 13px;
      }

      $ .prop-value a {
        color: #0b0080;
        text-decoration: none;
        cursor: pointer;
      }

      $ .qual-tab {
        border-collapse: collapse;
      }

      $ .qual-row {
      }

      $ .qprop-name {
        font-size: 13px;
        vertical-align: top;
        padding-left: 30px;
        width: 150px;
      }

      $ .qprop-name a {
        color: #0b0080;
        text-decoration: none;
        cursor: pointer;
      }

      $ .qprop-values {
        vertical-align: top;
      }

      $ .qprop-value {
        font-size: 13px;
        vertical-align: top;
      }

      $ .qprop-value a {
        color: #0b0080;
        text-decoration: none;
        cursor: pointer;
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

    out.push('<table>');
    for (const item of items) {
      out.push('<tr><td>');
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
      out.push('</td></tr>');
    }
    out.push('</table>');

    return out.join("");
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        font-size: 16px;
      }

      $ table {
        border-collapse: collapse;
        width: 100%;
      }

      $ tr {
        border-top: 1px solid lightgrey;
      }

      $ td {
        padding: 8px;
      }
    `;
  }
}

Component.register(KbItemList);

class KbItemCard extends MdCard {
  visible() {
    let item = this.state;
    return item;
  }

  onupdate() {
    let item = this.state;
    this.find("#title").update(item.text);
    this.find("#ref").update({url: wikiurl(item.ref), text: item.ref});
    this.find("#description").update(item.description);
    this.find("#datatype").update(item.type ? "Datatype: " + item.type : "");
  }

  static stylesheet() {
    return MdCard.stylesheet() + `
      $ #title {
        display: block;
        margin-top: 10px;
        font-size: 24px;
      }

      $ #ref a {
        display: block;
        font-size: 13px;
        color: #808080;
        margin-bottom: 10px;
        text-decoration: none;
      }

      $ #description {
        font-size: 16px;
      }

      $ #datatype {
        margin-top: 20px;
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

const commons_service = "https://commons.wikimedia.org/w/api.php?" +
                        "action=query&prop=imageinfo&iiprop=url&redirects&" +
                        "format=json&iiurlwidth=640&origin=*&titles=File:"

const commons_url = "https://commons.wikimedia.org/wiki/File:";

class KbPictureCard extends MdCard {
  visible() {
    let item = this.state;
    return item && item.thumbnail;
  }

  onupdated() {
    let item = this.state;
    let thumbnail = item.thumbnail;
    if (!thumbnail) return;

    // Clear picture while loading.
    let a = this.find("a");
    let img = this.find("img");
    a.href = "";
    img.src = "";

    // Get image info from Wikipedia Commons.
    fetch(commons_service + encodeURIComponent(thumbnail))
      .then(response => response.json())
      .then((data) => {
        for (let p in data.query.pages) {
          let page = data.query.pages[p];
          img.src = page.imageinfo[0].thumburl;
          a.href = commons_url + encodeURIComponent(thumbnail);
          break;
        }
      })
      .catch((error) => {
        console.error('error', error);
      });
  }

  render() {
    return '<a href="" target="_blank"><img src=""></a>';
  }

  static stylesheet() {
    return MdCard.stylesheet() + `
      $ {
        text-align: center;
      }

      $ img {
        max-width: 100%;
        max-height: 480px;
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
      $ .prop-tab {
        border-collapse: collapse;
        table-layout: fixed;
        font-size: 13px;
      }

      $ .prop-name {
        font-weight: normal;
        width: 40%;
        padding: 8px;
        vertical-align: top;
      }

      $ .prop-values {
        max-width: 0;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
      }

      $ .qprop-name {
        font-size: 11px;
        vertical-align: top;
        padding-left: 20px;
        width: 100px;
      }

      $ .qprop-value {
        font-size: 11px;
      }
    `;
  }
}

Component.register(KbXrefCard);


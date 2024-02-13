// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {inform} from "/common/lib/material.js";
import {Frame, QString} from "/common/lib/frame.js";
import {store, frame, settings} from "/common/lib/global.js";
import {Time, LabelCollector, latlong} from "/common/lib/datatype.js";
import {Document} from "/common/lib/document.js";
import {
  PhotoGallery,
  censor,
  imageurl,
  mediadb,
  hasthumb} from "/common/lib/gallery.js";

import {url_format} from "./schema.js";
import {get_widget} from "./plugins.js";

const n_id = store.id;
const n_is = store.is;
const n_isa = store.isa;
const n_name = frame("name");
const n_alias = frame("alias");
const n_description = frame("description");

const n_target = frame("target");
const n_media = frame("media");
const n_internal = frame("internal");
const n_casefile = frame("casefile");
const n_main = frame("main");
const n_lex = frame("lex");
const n_bookmarked = frame("bookmarked");

const n_item_type = frame("/w/item");
const n_lexeme_type = frame("/w/lexeme");
const n_string_type = frame("/w/string");
const n_xref_type = frame("/w/xref");
const n_time_type = frame("/w/time");
const n_url_type = frame("/w/url");
const n_media_type = frame("/w/media");
const n_quantity_type = frame("/w/quantity");
const n_geo_type = frame("/w/geo");

const n_amount = frame("/w/amount");
const n_unit = frame("/w/unit");
const n_lat = frame("/w/lat");
const n_lng = frame("/w/lng");
const n_date_of_birth = frame("P569");
const n_date_of_death = frame("P570");
const n_formatter_url = frame("P1630");
const n_media_legend = frame("P2096");
const n_has_quality = frame("P1552");
const n_not_safe_for_work = frame("Q2716583");
const n_popularity = frame("/w/item/popularity");
const n_fanin = frame("/w/item/fanin");

class KbLink extends Component {
  onconnected() {
    this.bind(null, "click", e => this.onclick(e));
  }

  onclick(e) {
    e.preventDefault();
    e.stopPropagation();
    var position;
    if (e.ctrlKey) {
      let source = this.match("topic-card");
      if (source) position = source.state;
    }
    this.dispatch("navigate", {ref: this.attrs.ref, position, event: e}, true);
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

class KbRef extends Component {
  onconnected() {
    this.bind(null, "click", e => this.onclick(e));
  }

  onclick(e) {
    e.preventDefault();
    e.stopPropagation();
    window.open(`${settings.kbservice}/kb/${this.state}`, "_blank");
  }

  render() {
    return Component.escape(this.state);
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

Component.register(KbRef);

class PropertyPanel extends Component {
  visible() {
    return this.state && this.state.length > 0;
  }

  render() {
    let item = this.state;
    let store = item.store;
    let h = [];

    let birth = store.resolve(item.get(n_date_of_birth));
    let born = birth ? new Time(birth) : null;
    let death = store.resolve(item.get(n_date_of_death));
    let died = death ? new Time(death) : null;

    function render_name(prop) {
      if (prop instanceof Frame) {
        let name = prop.get(n_name);
        if (!name) name = prop.id;
        h.push(`<kb-link ref="${prop.id}">`);
        h.push(Component.escape(name));
        h.push('</kb-link>');
      } else {
        h.push(Component.escape(prop.toString()));
      }
    }

    function render_quantity(val) {
      var amount, unit;
      if (val instanceof Frame) {
        amount = val.get(n_amount);
        unit = val.get(n_unit);
      } else {
        amount = val;
      }

      if (typeof amount === 'number') {
        amount = Math.round(amount * 1000) / 1000;
      }

      h.push(Component.escape(amount));
      if (unit) {
        h.push(" ");
        render_value(unit);
      }
    }

    function render_time(val, prop) {
      let t = new Time(val);
      let text = t.text();
      if (text) {
        h.push(` <span class="prop-time">${text}</span>`);
        if (born) {
          if (prop == n_date_of_birth) {
            if (!died) {
              let age = t.age(new Time(new Date()));
              if (age) {
                h.push(` <span class="prop-age">(${age} yo)</span>`);
              }
            }
          } else {
            let years = born.age(t);
            if (years) {
              h.push(` <span class="prop-age">(${years} yo)</span>`);
            }
          }
        }
      } else {
        render_fallback(val);
      }
    }

    function render_link(val) {
      let name = val.get(n_name);
      if (!name) {
        if (val.get(n_isa) == n_casefile) {
          let main = val.get(n_main);
          if (main) name = main.get(n_name);
        }
        if (!name) name = val.id;
      }
      if (!name) name = "???";

      h.push(`<kb-link ref="${val.id}">`);
      h.push(Component.escape(name));
      h.push('</kb-link>');
    }

    function render_text(val) {
      if (val instanceof QString) {
        h.push(Component.escape(val.text));
        if (val.qual) {
          let lang = val.qual.get(n_name);
          if (!lang) lang = val.qual.id;
          h.push(' <span class="prop-lang">[');
          render_value(lang);
          h.push(']</span>');
        }
      } else if (val instanceof Frame) {
        render_link(val);
      } else {
        h.push(Component.escape(val));
      }
    }

    function render_xref(val, prop) {
      let url = url_format(prop, val);
      if (url) {
        h.push('<a href="');
        h.push(url);
        h.push('" target="_blank" rel="noreferrer">');
        render_value(val);
        h.push('</a>');
      } else {
        render_value(val);
      }
    }

    function render_url(val) {
      let url = val;
      if (typeof val === "string") {
        if (val.startsWith("https://drive.ringgaard.com/")) {
          val = val.slice(28);
          let slash = val.lastIndexOf("/");
          if (slash != -1) val = val.slice(slash + 1);
        }
        if (val.startsWith("http://")) val = val.slice(7);
        if (val.startsWith("https://")) val = val.slice(8);
        if (val.startsWith("www.")) val = val.slice(4);
        if (val.startsWith("mailto:")) val = val.slice(7);
        if (val.endsWith("/")) val = val.slice(0, -1);
      }
      h.push('<a href="');
      h.push(url);
      h.push('" target="_blank" rel="noreferrer">');
      render_value(val);
      h.push('</a>');
    }

    function render_coord(val) {
      let lat = val.get(n_lat);
      let lng = val.get(n_lng);
      let url = `https://www.google.com/maps?q=loc:${lat},${lng}`;

      h.push('<a href="');
      h.push(url);
      h.push('" target="_blank" rel="noreferrer">');
      h.push(latlong(lat, lng));
      h.push('</a>');
    }

    function render_fallback(val) {
      if (val instanceof Frame) {
        if (val.isanonymous()) {
          if (val.has(n_amount)) {
            render_quantity(val);
          } else {
            h.push(Component.escape(val.text()));
          }
        } else {
          render_link(val);
        }
      } else {
        render_text(val);
      }
    }

    function render_value(val, prop) {
      let dt = undefined;
      if (prop && (prop instanceof Frame)) dt = prop.get(n_target);
      switch (dt) {
        case n_item_type:
          if (val instanceof Frame) {
            render_link(val);
          } else {
            render_fallback(val);
          }
          break;
        case n_xref_type:
        case n_media_type:
          render_xref(val, prop);
          break;
        case n_time_type:
          render_time(val, prop);
          break;
        case n_quantity_type:
          render_quantity(val);
          break;
        case n_string_type:
          if (prop && prop.has(n_formatter_url)) {
            render_xref(val, prop);
          } else {
            render_text(val);
          }
          break;
        case n_url_type:
          render_url(val);
          break;
        case n_geo_type:
          if (val instanceof Frame) {
            render_coord(val);
          } else {
            render_text(val);
          }
          break;
        default:
          render_fallback(val);
      }
    }

    let prev = undefined;
    h.push("<table>");
    for (let [name, value] of item) {
      if (name !== prev) {
        // End previous property group.
        if (prev !== undefined) h.push('</td></tr>');

        // Start new property group for new property.
        h.push('<tr class="prop-row">');

        // Property name.
        if (name === null) {
          h.push('<td class="prop-notes" colspan="2">');
        } else {
          h.push('<td class="prop-name">');
          render_name(name);
          h.push('</td>');
          h.push('<td class="prop-values">');
        }
        prev = name;
      }

      // Property value.
      let v = store.resolve(value);
      if (name === null) {
        h.push('<div class="prop-note">');
        render_value(v, name);
        h.push('</div>');
      } else {
        h.push('<div class="prop-value">');
        render_value(v, name);
        h.push('</div>');
      }

      // Qualifiers.
      if (v != value) {
        h.push('<table class="qual-tab">');
        let qprev = undefined;
        for (let [qname, qvalue] of value) {
          if (qname == n_is) continue;

          if (qname != qprev) {
            // Start new property group for new property.
            if (qprev !== undefined) h.push('</td></tr>');
            h.push('<tr class="qual-row">');

            // Qualified property name.
            if (qname === null) {
              h.push('<td class="qprop-notes" colspan="2">');
            } else {
              h.push('<td class="qprop-name">');
              render_name(qname);
              h.push('</td>');
              h.push('<td class="qprop-values">');
            }
            qprev = qname;
          }

          // Qualified property value.
          if (qname === null) {
            h.push('<div class="qprop-note">');
            render_value(qvalue, qname);
            h.push('</div>');
          } else {
            h.push('<div class="qprop-value">');
            render_value(qvalue, qname);
            h.push('</div>');
          }
        }
        if (qprev !== undefined) h.push('</td></tr>');
        h.push('</table>');
      }
    }
    if (prev !== undefined) h.push('</td></tr>');
    h.push("</table>")

    return h.join("");
  }

  static stylesheet() {
    return `
      $ table {
        font-size: 16px;
        border-collapse: collapse;
        width: 100%;
      }

      $ .prop-row {
        border-top: thin solid lightgrey;
      }

      $ .prop-row:first-child {
        border-top: none;
      }

      $ .prop-name {
        font-weight: 500;
        width: 25%;
        padding: 8px;
        vertical-align: top;
        overflow-wrap: break-word;
      }

      $ .prop-values {
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

      $ .prop-time {
        color: #0b0080;
      }

      $ .prop-value a {
        color: #0b0080;
        text-decoration: none;
        cursor: pointer;
        outline: none;
      }

      $ .prop-notes {
        padding: 8px 0px 8px 8px;
      }

      $ .prop-note {
        display: list-item;
        margin-left: 16px;
      }

      $ .qual-tab {
        border-collapse: collapse;
      }

      $ .qual-row {
      }

      $ .qprop-name {
        font-size: 13px;
        vertical-align: top;
        padding: 1px 3px 1px 30px;
        width: 150px;
        user-select: none;
      }

      $ .qprop-values {
        vertical-align: top;
      }

      $ .qprop-value {
        font-size: 13px;
        vertical-align: top;
        padding: 1px;
      }

      $ .qprop-value a {
        color: #0b0080;
        text-decoration: none;
        cursor: pointer;
        outline: none;
      }

      $ .qprop-notes {
        vertical-align: top;
      }

      $ .qprop-note {
        display: list-item;
        font-size: 13px;
        margin-left: 44px;
      }
    `;
  }
}

Component.register(PropertyPanel);

class XrefPanel extends PropertyPanel {
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
        padding: 1px;
      }
    `;
  }
}

Component.register(XrefPanel);

class DocumentItem extends Component {
  onconnected() {
    this.attach(this.onclick, "click");
  }

  async onclick(e) {
    let sidebar = this.match("#editor").sidebar;
    let doc = new Document(store, this.state.source, this.state.context);
    sidebar.goto(doc);
  }

  render() {
    if (!this.state) return;
    let source = this.state.source;
    let context = this.state.context;
    let name = source instanceof Frame && source.get(n_name);
    if (!name && context) name = context.topic.get(n_name);
    if (!name) name = `Document ${context.index + 1}`;
    let bookmarked = source instanceof Frame && source.get(n_bookmarked);

    return `
      <md-icon icon="description"></md-icon>
      <div>${Component.escape(name)}</div>
      <md-spacer></md-spacer>
      ${bookmarked ? '<md-icon icon="bookmark"></md-icon>' : ''}
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        align-items: center;
        cursor: pointer;
        font-size: 16px;
        padding: 8px;
      }
      $:hover {
        background-color: #eeeeee;
      }
    `;
  }
}

Component.register(DocumentItem);

class DocumentPanel extends Component {
  visible() {
    return this.state && this.state.length > 0;
  }

  render() {
    if (!this.state) return;
    let h = new Array();
    for (let d of this.state) {
      h.push(new DocumentItem({source: d.source, context: d.context}));
    }
    return h;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        flex-direction: column;
        border: 1px solid lightgray;
        margin: 8px;
      }
    `;
  }
}

Component.register(DocumentPanel);

class PicturePanel extends Component {
  onconnected() {
    this.bind(null, "click", e => this.onopen(e));
  }

  onupdated() {
    let thumb = null;
    let index = 0;

    let images = this.state;
    if (images && images.length > 0) {
      for (let i = 0; i < images.length; ++i) {
        if (!images[i].nsfw && hasthumb(images[i].url)) {
          index = i;
          break;
        }
      }
      if (hasthumb(images[index].url)) thumb = images[index];
    }

    if (thumb) {
      let caption = thumb.text;
      if (caption) {
        caption = caption.toString().replace(/\[\[|\]\]/g, '');
      }

      if (images?.length > 1) {
        if (!caption) caption = "";
        caption += ` [${index + 1}/${images.length}]`;
      }

      this.find(".photo").update(imageurl(thumb.url, true));
      this.find(".caption").update(caption);
      this.find("#nothumb").update(false);
    } else {
      this.find(".photo").update(null);
      this.find(".caption").update(null);
      this.find("#nothumb").update(true);
    }
  }

  visible() {
    return this.state && this.state.length > 0;
  }

  onopen(e) {
    e.stopPropagation();
    let modal = new PhotoGallery();
    for (let event of ["nsfw", "sfw", "delimage", "picedit"]) {
      modal.bind(null, event, e => this.dispatch(e.type, e.detail, true));
    }
    modal.open(this.state);
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

      $ #nothumb {
        font-size: 64px;
      }

      $ img {
        max-width: 100%;
        max-height: ${settings.picturesize || "320px"};
        vertical-align: middle;
        user-select: none;
      }
    `;
  }
}

Component.register(PicturePanel);

class TopicExpander extends Component {
  onconnected() {
    this.bind(null, "click", e => this.onclick(e));
  }

  onclick(e) {
    if (this.expansion) {
      this.close();
    } else {
      this.open();
    }
  }

  async open() {
    let item = this.state;
    if (typeof(item) === 'string') item = frame(item);

    // Retrieve item if needed.
    if (!item.ispublic()) {
      let url = `${settings.kbservice}/kb/topic?id=${item.id}`;
      let response = await fetch(url);
      if (response.status == 200) {
        item = await store.parse(response);
      } else if (response.status == 404 && item.id.match(/Q\d+/)) {
        window.open(`https://www.wikidata.org/wiki/${item.id}`, "_blank",
                    "noopener,noreferrer");
        return;
      } else {
        inform("Error fetching item", item.id);
        return;
      }
    }

    // Retrieve labels.
    let labels = new LabelCollector(store)
    labels.add(item);
    await labels.retrieve();

    // Add item panel for subtopic.
    let panel = this.match("subtopic-panel");
    this.expansion = new ItemPanel(item);
    panel.appendChild(this.expansion);
    this.update(this.state);
  }

  close() {
    // Add item panel for subtopic.
    let panel = this.match("subtopic-panel");
    panel.removeChild(this.expansion);
    this.expansion = undefined;
    this.update(this.state);
  }

  render() {
    let topic = this.state;
    if (!topic) return;
    let itemid = topic.id || topic;
    return `
      <div>${Component.escape(itemid)}</div>
      <md-icon icon="${this.expansion ? "expand_less" : "expand_more"}">
      </md-icon>
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        align-items: center;
        cursor: pointer;
        font-size: 13px;
        color: #808080;
        user-select: none;
      }
    `;
  }
}

Component.register(TopicExpander);

class TopicBar extends Component {
  visible() {
    return this.state && this.state.length > 0;
  }

  render() {
    if (!this.state) return;
    let h = new Array();
    for (let subtopic of this.state) {
      h.push(new TopicExpander(subtopic));
    }
    return h;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        justify-content: space-evenly;
        flex-direction: row;
        flex-wrap: wrap;
      }
    `;
  }
}

Component.register(TopicBar);

class SubtopicPanel extends Component {
  visible() {
    return this.state && this.state.length > 0;
  }

  render() {
    return new TopicBar(this.state);
  }

  static stylesheet() {
    return `
      $ {
        display: block;
      }
    `;
  }
}

Component.register(SubtopicPanel);

class WidgetPanel extends Component {
  visible() {
    return this.state;
  }

  render() {
    return this.state;
  }

  static stylesheet() {
    return `
      $ {
        display: block;
        padding: 0px;
      }
    `;
  }
}

Component.register(WidgetPanel);

class ItemPanel extends Component {
  visible() {
    return this.state && this.state.length > 0;
  }

  onconnected() {
    if (this.state) this.onupdated();
  }

  async onupdated() {
    // Split item into properties, media, xrefs, and subtopics.
    let item = this.state;
    if (!item) return;
    let top = this.parentNode?.closest("item-panel") == null;
    let readonly = this.closest("topic-card")?.readonly;
    let id = null;
    let names = new Array();
    let description = null;
    let title = item.get(n_name);
    let props = new Frame(store);
    let xrefs = new Frame(store);
    let gallery = [];
    let subtopics = new Array();
    let docs = new Array();
    for (let [name, value] of item) {
      if (name === n_media) {
        let url = store.resolve(value);
        let nsfw = url.startsWith('!');
        if (nsfw) url = url.slice(1);
        if (value instanceof Frame) {
          if (value.has(n_has_quality, n_not_safe_for_work)) nsfw = true;
          gallery.push({url, nsfw, text: value.get(n_media_legend)});
        } else {
          gallery.push({url, nsfw});
        }
      } else if (name === n_id) {
        id = value;
      } else if (name === n_is) {
        subtopics.push(value);
      } else if (name === n_name || name === n_alias) {
        let n = store.resolve(value).toString();
        if (!top || n != title) names.push(n);
      } else if (name === n_description) {
        description = value;
      } else if (name === n_lex) {
        let context = {topic: item, index: docs.length, readonly};
        docs.push({source: value, context});
      } else if (name === n_internal) {
        // Skip internals.
      } else if (name === n_popularity || name === n_fanin) {
        // Skip ḿetrics.
      } else if ((name instanceof Frame) && name.get(n_target) == n_xref_type) {
        xrefs.add(name, value);
      } else {
        props.add(name, value);
      }
    }

    // Censor photos.
    gallery = censor(gallery, settings.nsfw);

    // Get widget.
    let widget = await get_widget(item);

    // Update panels.
    this.find("#identifier").update(id);
    this.find("#names").update(names.join(" • "));
    this.find("#description").update(description);
    this.find("#properties").update(props);
    this.find("#documents").update(docs);
    this.find("#widget").update(widget);
    this.find("#picture").update(gallery);
    this.find("#xrefs").update(xrefs);
    this.find("#subtopics").update(subtopics);

    // Update rulers.
    if (props.length == 0 || (gallery.length == 0 && xrefs.length == 0)) {
      this.find("#vruler").style.display = "none";
    } else {
      this.find("#vruler").style.display = "";
    }
    if (gallery.length == 0 || xrefs.length == 0) {
      this.find("#hruler").style.display = "none";
    } else {
      this.find("#hruler").style.display = "";
    }
  }

  render() {
    return `
      <div>
        <kb-ref id="identifier"></kb-ref>:
        <md-text id="names"></md-text>
      </div>
      <div><md-text id="description"></md-text></div>
      <div class="columns">
        <div class="left">
          <property-panel id="properties">
          </property-panel>
          <document-panel id="documents">
          </document-panel>
        </div>

        <div id="vruler"></div>

        <div class="right">
          <widget-panel id="widget"></widget-panel>
          <picture-panel id="picture">
            <md-image class="photo"></md-image>
            <md-icon id="nothumb" icon="photo_library"></md-icon>
            <md-text class="caption"></md-text>
          </picture-panel>
          <div id="hruler"></div>
          <xref-panel id="xrefs">
          </xref-panel>
        </div>
      </div>
      <subtopic-panel id="subtopics"></subtopic-panel>
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: block;
      }

      $ #identifier {
        font-size: 13px;
        color: #808080;
        padding-left: 5px;
      }

      $ #names {
        font-size: 13px;
        color: #808080;
      }

      $ #description {
        font-size: 16px;
        padding-left: 5px;
        padding-right: 5px;
        display: block;
      }

      $ .columns {
        display: flex;
        flex-direction: row;
      }

      $ .left {
        display: flex;
        flex: 1 1 66%;
        flex-direction: column;
      }

      $ .right {
        display: flex;
        flex: 1 1 33%;
        flex-direction: column;
      }

      $ #vruler {
        background-color: lightgrey;
        width: 1px;
        max-width: 1px;
        margin-left: 10px;
        margin-right: 10px;
      }

      $ #hruler {
        background-color: lightgrey;
        height: 1px;
        max-height: 1px;
        margin-top: 10px;
        margin-bottom: 10px;
      }
    `;
  }
};

Component.register(ItemPanel);


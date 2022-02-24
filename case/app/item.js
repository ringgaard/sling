// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {Frame, QString} from "/common/lib/frame.js";
import {store, settings} from "./global.js";
import {Time, LabelCollector, latlong} from "./value.js";
import {PhotoGallery, censor, imageurl, mediadb} from "/common/lib/gallery.js";

mediadb.thumb = false;

const n_id = store.id;
const n_is = store.is;
const n_isa = store.isa;
const n_name = store.lookup("name");
const n_alias = store.lookup("alias");
const n_description = store.lookup("description");

const n_target = store.lookup("target");
const n_media = store.lookup("media");
const n_casefile = store.lookup("casefile");
const n_main = store.lookup("main");

const n_item_type = store.lookup("/w/item");
const n_lexeme_type = store.lookup("/w/lexeme");
const n_string_type = store.lookup("/w/string");
const n_xref_type = store.lookup("/w/xref");
const n_time_type = store.lookup("/w/time");
const n_url_type = store.lookup("/w/url");
const n_media_type = store.lookup("/w/media");
const n_quantity_type = store.lookup("/w/quantity");
const n_geo_type = store.lookup("/w/geo");

const n_amount = store.lookup("/w/amount");
const n_unit = store.lookup("/w/unit");
const n_lat = store.lookup("/w/lat");
const n_lng = store.lookup("/w/lng");
const n_date_of_birth = store.lookup("P569");
const n_date_of_death = store.lookup("P570");
const n_formatter_url = store.lookup("P1630");
const n_media_legend = store.lookup("P2096");
const n_has_quality = store.lookup("P1552");
const n_not_safe_for_work = store.lookup("Q2716583");

class KbLink extends Component {
  onconnected() {
    this.bind(null, "click", e => this.onclick(e));
  }

  onclick(e) {
    e.preventDefault();
    e.stopPropagation();
    this.dispatch("navigate", {ref: this.props.ref, event: e}, true);
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
    this.dispatch("navigate", {ref: this.state, event: e}, true);
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
  onconnected() {
    this.bind(null, "click", e => { e.stopPropagation(); });
  }

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
      h.push(t.text());
      if (born) {
        if (prop == n_date_of_birth) {
          if (!died) {
            let age = born.age(new Time(new Date()));
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
      if (name) {
        h.push(`<kb-link ref="${val.id}">`);
        h.push(Component.escape(name));
        h.push('</kb-link>');
      } else {
        render_fallback(val);
      }
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
      let formatter = prop.resolved(n_formatter_url);
      if (formatter) {
        let url = formatter.replace("$1", val);
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
      let url = `http://maps.google.com/maps?q=${lat},${lng}`;

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
          render_text(val);
          break;
        case n_url_type:
          render_url(val);
          break;
        case n_geo_type:
          render_coord(val);
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
        width: 20%;
        padding: 8px;
        vertical-align: top;
        overflow-wrap: break-word;
        user-select: none;
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
    return PropertyPanel.stylesheet() + `
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

class PicturePanel extends Component {
  onconnected() {
    this.bind(null, "click", e => this.onopen(e));
  }

  onupdated() {
    let images = this.state;
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
        caption = caption.toString().replace(/\[\[|\]\]/g, '');
      }
      if (images.length > 1) {
        if (!caption) caption = "";
        caption += ` [${index + 1}/${images.length}]`;
      }

      this.find(".photo").update(imageurl(image.url));
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

    // Retrieve item if needed.
    if (!item.ispublic()) {
      let url = `${settings.kbservice}/kb/topic?id=${item.id}`;
      let response = await fetch(url);
      if (response.status == 200) {
        item = await store.parse(response);
      } else {
        console.log("Error fetching item", item.id);
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
    return `
      <div>${Component.escape(topic.id)}</div>
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
}

Component.register(SubtopicPanel);

class ItemPanel extends Component {
  visible() {
    return this.state && this.state.length > 0;
  }

  onconnected() {
    if (this.state) this.onupdated();
  }

  onupdated() {
    // Split item into properties, media, xrefs, and subtopics.
    let item = this.state;
    if (!item) return;
    let top = this.parentNode && this.parentNode.closest("item-panel") == null;
    let id = null;
    let names = new Array();
    let description = null;
    let title = item.get(n_name);
    let props = new Frame(store);
    let xrefs = new Frame(store);
    let gallery = [];
    let subtopics = new Array();
    for (let [name, value] of item) {
      if (name === n_media) {
        if (value instanceof Frame) {
          gallery.push({
            url: store.resolve(value),
            text: value.get(n_media_legend),
            nsfw: value.get(n_has_quality) == n_not_safe_for_work,
          });
        } else {
          gallery.push({url: value});
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
      } else if ((name instanceof Frame) && name.get(n_target) == n_xref_type) {
        xrefs.add(name, value);
      } else {
        props.add(name, value);
      }
    }

    // Censor photos.
    gallery = censor(gallery, settings.nsfw);

    // Update panels.
    this.find("#identifier").update(id);
    this.find("#names").update(names.join(" • "));
    this.find("#description").update(description);
    this.find("#properties").update(props);
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
      <md-row-layout>
        <md-column-layout style="flex: 1 1 66%;">
          <property-panel id="properties">
          </property-panel>
        </md-column-layout>

        <div id="vruler"></div>

        <md-column-layout style="flex: 1 1 33%;">
          <picture-panel id="picture">
            <md-image class="photo"></md-image>
            <md-text class="caption"></md-text>
          </picture-panel>
          <div id="hruler"></div>
          <xref-panel id="xrefs">
          </xref-panel>
        </md-column-layout>
      </md-row-layout>
      <subtopic-panel id="subtopics"></subtopic-panel>
    `;
  }

  static stylesheet() {
    return `
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


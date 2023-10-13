// Copyright 2023 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Document viewer web component.

import {store, frame} from "./global.js";
import {Frame, QString} from "./frame.js";
import {Document} from "./document.js";
import {Component, stylesheet} from "./component.js";
import {value_text, LabelCollector} from "./datatype.js";
import "./material.js";

const n_is = store.is;
const n_isa = store.isa;
const n_phrases = frame("phrases");
const n_description = frame("description");

stylesheet("@import url(/common/font/anubis.css)");

function html_prop(prop) {
  let [text, encoded] = value_text(prop);
  return Component.escape(text);
}

function html_value(value, prop) {
  if (prop == n_is) prop = undefined;
  value = store.resolve(value);
  let [text, encoded] = value_text(value, prop);
  if (encoded) {
    let ref = value && value.id;
    if (!ref) {
      if (value instanceof Frame) {
        ref = value.text(false, true);
      } else if (value instanceof QString) {
        ref = value.stringify(store);
      } else {
        ref = value;
      }
    }
    return `<span class="link" ref="${ref}">${Component.escape(text)}</span>`;
  } else {
    return Component.escape(text);
  }
}

function isredirect(frame) {
  return frame.length == 1 && frame.name(0) == n_is;
}

function selecting() {
  var selection = window.getSelection();
  return selection && selection.type === 'Range';
}

class AnnotationBox extends Component {
  onconnected() {
    this.attach(this.onpointerdown, "pointerdown");
  }

  onpointerdown(e) {
    e.preventDefault();
    let target = e.target;
    let ref = target.getAttribute("ref");
    let mention = this.state;

    if (ref) {
      this.dispatch("navigate", {ref, event: e}, true);
    } else if (target.id == "annotate") {
      this.dispatch("annotate", {mention, event: e}, true);
    } else if (target.id == "reconcile") {
      this.dispatch("reconcile", {mention, event: e}, true);
    } else if (target.id == "highlight") {
      this.dispatch("highlight", {mention, event: e}, true);
    } else if (target.id == "copy") {
      let text = mention.text(true);
      if (e.ctrlKey && (mention.annotation instanceof Frame)) {
        let item = mention.annotation.resolve();
        if (item.id) text = item.id;
      }
      navigator.clipboard.writeText(text);
    }
    this.remove();
  }

  render() {
    let mention = this.state;
    let annotation = mention.annotation;

    let h = new Array();
    h.push(`
      <div>
        <md-icon icon="add_circle" class="action" id="annotate"></md-icon>
        <md-icon icon="join_right" class="action" id="reconcile"></md-icon>
        <md-icon icon="content_copy" class="action" id="copy"></md-icon>
        <md-icon icon="flag" class="action" id="highlight"></md-icon>
      </div>
    `);

    if (typeof(annotation) === 'string') {
      let url = annotation;
      let anchor = Component.escape(url);
      h.push(`<div class="url"><a href="${url}">${anchor}</a></div>`);
    } else if ((annotation instanceof Frame) && annotation.length > 0) {
      if (annotation.isanonymous() && !isredirect(annotation)) {
        let dt = annotation.get(n_isa);
        if (dt) {
          let [text, encoded] = value_text(store.resolve(annotation), null, dt);
          let v = Component.escape(text);
          h.push(`<div class="value">${v}</div>`);
        } else {
          for (let [name, value] of annotation) {
            value = store.resolve(value);
            let p = html_prop(name);
            let v;
            if ((value instanceof Frame) && value.isanonymous()) {
              let m = mention.document.mention_of(value);
              if (m) {
                let label = Component.escape(m.text(true));
                v = `<span class="docref">${label}</span>`;
              }
            }
            if (!v) v = html_value(value, name);
            h.push(`<div class="prop">${p}: ${v}</div>`);
          }
        }
      } else {
        let item = store.resolve(annotation);
        if (item.length > 0) {
          let v = html_value(item);
          h.push(`<div class="link">${v}</div>`);
          let decription = item.get(n_description);
          if (!decription) {
            let link = item.link();
            if (link) decription = link.get(n_description);
          }
          if (decription) {
            h.push(`<div class="descr">${Component.escape(decription)}</div>`);
          }
        }
      }
    }
    return h.join("");
  }

  static stylesheet() {
    return `
      $ {
        position: absolute;
        left: 0;
        top: 1em;
        z-index: 1;
        display: flex;
        flex-direction: column;

        font-family: Roboto,Helvetica,sans-serif;
        font-size: 16px;
        font-weight: normal;
        line-height: 1;

        color: black;
        background: white;
        border: 1px solid #a0a0a0;
        box-shadow: 0 14px 28px rgba(0,0,0,0.25), 0 10px 10px rgba(0,0,0,0.22);

        padding: 8px;
        cursor: default;
      }
      $ .title {
        display: flex;
        align-items: center;
      }
      $ .url {
        padding-top: 4px;
      }
      $ .prop {
        padding-top: 4px;
      }
      $ .docref {
        color: #0000dd;
        font-style: italic;
        cursor: default;
      }
      $ .docref:hover {
        text-decoration: none;
      }
      $ .link {
        color: #0000dd;
        cursor: pointer;
      }
      $ .descr {
        font-size: 12px;
      }
      $ link:hover {
        text-decoration: underline;
      }
      $ .value {
        color: #0000dd;
      }
      $ .action {
        padding: 4px;
        font-size: 20px;
        font-weight: normal;
        color: #808080;
        cursor: pointer;
        user-select: inherit;
      }
      $ .action:hover {
        text-decoration: none;
        background-color: #eeeeee;
      }
    `;
  }
};

Component.register(AnnotationBox);

export class DocumentViewer extends Component {
  onconnected() {
    this.attach(this.onmouse, "mouseover");
    this.attach(this.clear_popup, "mouseleave");
    this.attach(this.clear_popup, "selectstart");
    this.attach(this.onclick, "click");
  }

  async onmouse(e) {
    // Ignore if selecting text or ctrl or shift is down.
    if (selecting()) {
      this.clear_popup();
      return;
    }
    if (e.ctrlKey || e.shiftKey) return;

    // Find first enclosing span.
    let span = e.target;
    while (span != this) {
      if (span == this.popup) return;
      if (span.tagName === 'SPAN') break;
      span = span.parentElement;
    }

    // Close existing popup.
    if (this.popup && !this.popup.contains(span)) {
      this.clear_popup();
    }

    // Get mention for span.
    let mid = span.getAttribute("mention");
    if (!mid) return;
    let doc = this.state;
    let mention = doc.mentions[parseInt(mid)];

    // Fetch labels for annotations.
    if (mention.annotation instanceof Frame) {
      let collector = new LabelCollector(store);
      if (mention.annotation.isanonymous()) {
        collector.add(mention.annotation);
      } else {
        collector.add_item(mention.annotation);
      }
      await collector.retrieve();
    }

    // Open new annotation box popup.
    this.clear_popup();
    this.popup = new AnnotationBox(mention);
    this.append(this.popup);

    // Get paragraph line height.
    if (!this.lineheight) {
      this.lineheight = this.find(".linemeasure").offsetHeight;
    }
    // Adjust annotation box position.
    const boxwidth = Math.max(span.offsetWidth, 160);
    let top = span.offsetTop + span.offsetHeight;
    let left = span.offsetLeft;
    if (span.offsetHeight >= 2 * this.lineheight) {
      // Mention wraps around to next line.
      left = 0;
    } else {
      let overflow = left + boxwidth - this.offsetWidth;
      if (overflow > 0) {
        left -= overflow;
        if (left < 0) left = 0;
      }
    }
    this.popup.style.top = `${top}px`;
    this.popup.style.left = `${left}px`;
  }

  onclick(e) {
    if (selecting()) {
      this.clear_popup();
      return;
    }

    let target = e.target;
    let mid = target.getAttribute("mention");
    if (!mid) return;
    let doc = this.state;
    let mention = doc.mentions[parseInt(mid)];
    let annotation = store.resolve(mention.annotation)

    if (e.ctrlKey) {
      this.dispatch("annotate", {mention, event: e}, true);
    } else if (annotation && annotation.id) {
      this.dispatch("navigate", {ref: annotation.id, event: e}, true);
    } else {
      this.dispatch("reconcile", {mention, event: e}, true);
    }
    e.stopPropagation();
  }

  clear_popup() {
    if (this.popup) {
      this.popup.remove();
      this.popup = null;
    }
  }

  visible() { return this.state && this.state.source; }

  goto(topic) {
    // Find first mention.
    let doc = this.state;
    let m = doc.first_mention(topic);
    if (!m) return;

    // Scroll mention into view.
    let span = this.querySelector(`span[mention="${m.index}"]`);
    if (span) span.scrollIntoView({block: "center"});
  }

  render() {
    let doc = this.state;
    if (!doc) return;

    let h = new Array();

    // Spans sorted by begin and end positions.
    let starts = doc.mentions.slice();
    let ends = doc.mentions.slice();
    starts.sort((a, b) => a.begin - b.begin || b.end - a.end);
    ends.sort((a, b) => a.end - b.end || b.begin - a.begin);

    // Generate HTML with spans.
    let from = 0;
    let text = doc.text;
    let n = doc.mentions.length;
    let si = 0;
    let ei = 0;
    let level = 0;
    let match = doc.context && doc.context.match;
    for (let pos = 0; pos < text.length; ++pos) {
      // Output span ends.
      while (ei < n && ends[ei].end < pos) ei++;
      while (ei < n && ends[ei].end == pos) {
        if (from < pos) {
          h.push(text.slice(from, pos));
          from = pos;
        }
        level--;
        h.push("</span>");
        ei++;
      }

      // Output span starts.
      while (si < n && starts[si].begin < pos) si++;
      while (si < n && starts[si].begin == pos) {
        if (from < pos) {
          h.push(text.slice(from, pos));
          from = pos;
        }
        level++;
        let mention = starts[si];
        if (match && store.resolve(mention.annotation) == match) {
          h.push(`<span class="l${level} highlight" mention=${mention.index}>`);
        } else {
          h.push(`<span class="l${level}" mention=${mention.index}>`);
        }
        si++;
      }
    }
    h.push(text.slice(from));
    while (ei++ < n) h.push("</span>");

    // Output ghost paragraph for line height measurement.
    h.push('<p><span class="linemeasure">M</span></p>');

    // TODO: output themes
    return h.join("");
  }

  static stylesheet() {
    return `
      $ {
        font: 1rem anubis, serif;
        line-height: 1.5;
        padding: 4px 8px;
        position: relative;
      }
      $ .linemeasure {
        visibility: hidden;
      }
      $ p {
        margin-right: 8px;
      }
      $ span {
        color: #0000dd;
        cursor: pointer;
      }
      $ span.highlight {
        background-color: #fce94f;
        padding: 4px 0 4px 0;
      }
      $ span:hover {
        text-decoration: underline;
      }
      $ span.l1:hover .l1 {
        color: blue;
      }
      $ span.l1:hover .l2 {
        color: green;
      }
      $ span.l1:hover .l3 {
        color: red;
      }
      $ span.l1:hover .l4 {
        color: orange;
      }
      $ aside {
        background-color: #ecf0f1;
        padding: 6px;
        margin: 0px 24px;
      }
    `;
  }
};

Component.register(DocumentViewer);


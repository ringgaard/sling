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

stylesheet("@import url(/common/font/anubis.css)");

function html_prop(prop) {
  let [text, encoded] = value_text(prop);
  return Component.escape(text);
}

function html_value(value, prop) {
  if (prop == n_is) prop = undefined;
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
    } else if (target.className == "annotate") {
      this.dispatch("annotate", {mention, event: e}, true);
    }
    if (target.tagName != "A") this.remove();
  }

  render() {
    let mention = this.state;
    let annotation = mention.annotation;
    let phrase = mention.text(true);

    let h = new Array();
    h.push(`<div class="title">
      <span class="phrase">${Component.escape(phrase)}</span>
      <md-icon icon="add" class="annotate"></md-icon>
    </div>`);

    if (typeof(annotation) === 'string') {
      let url = annotation;
      let anchor = Component.escape(url);
      h.push(`<div class="url"><a href="${url}">${anchor}</a></div>`);
    } else if (annotation instanceof Frame) {
      if (annotation.isanonymous()) {
        let dt = annotation.get(n_isa);
        if (dt) {
          let [text, encoded] = value_text(store.resolve(annotation), null, dt);
          let v = Component.escape(text);
          h.push(`<div class="value">${v}</div>`);
        } else {
          for (let [name, value] of annotation) {
            let m = mention.document.mapping.get(value);
            let p = html_prop(name);
            let v;
            if (m) {
              let label = Component.escape(m.text(true));
              v = `<span class="docref">${label}</span>`;
            } else {
             v = html_value(value, name);
           }
            h.push(`<div class="prop">${p}: ${v}</div>`);
          }
        }
      } else {
        let v = html_value(store.resolve(annotation));
        h.push(`<div class="link">${v}</div>`);
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
        background: #f8f8f8;
        border: 1px solid #a0a0a0;
        box-shadow: 0 14px 28px rgba(0,0,0,0.25), 0 10px 10px rgba(0,0,0,0.22);

        padding: 8px;
        min-width: 300px;
        cursor: default;
      }
      $ .title {
        display: flex;
        align-items: center;
      }
      $ .phrase {
        font-style: italic;
        padding: 4px 4px 4px 0px;
      }
      $ .phrase:hover {
        text-decoration: none;
        cursor: default;
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
      $ link:hover {
        text-decoration: underline;
      }
      $ .value {
        color: #0000dd;
      }
      $ .annotate {
        padding: 4px;
        font-size: 20px;
        font-weight: normal;
        cursor: pointer;
        user-select: inherit;
      }
      $ .annotate:hover {
        text-decoration: none;
      }
    `;
  }
};

Component.register(AnnotationBox);

export class DocumentViewer extends Component {
  onconnect() { this.onupdate(); }
  onupdate() {
    if (this.state) {
      this.doc = new Document(store, this.state.source, this.state.context);
    } else {
      this.doc = null;
    }
  }

  onrendered() {
    this.attach(this.onmouse, "mouseover");
    this.attach(this.onleave, "mouseleave");
  }

  async onmouse(e) {
    // Ignore if selecting text or ctrl or shift is down.
    var selection = window.getSelection();
    if (selection && selection.type === 'Range') return;
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
    let mention = this.doc.mentions[parseInt(mid)];

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
    span.append(this.popup);
  }

  onleave(e) {
    // Close popups when leaving document viewer.
    this.clear_popup();
  }

  clear_popup() {
    if (this.popup) {
      this.popup.remove();
      this.popup = null;
    }
  }

  visible() { return this.state; }

  render() {
    if (!this.doc) return;
    let h = new Array();

    // Spans sorted by begin and end positions.
    let starts = this.doc.mentions.slice();
    let ends = this.doc.mentions.slice();
    starts.sort((a, b) => a.begin - b.begin || b.end - a.end);
    ends.sort((a, b) => a.end - b.end || b.begin - a.begin);

    // Generate HTML with spans.
    let from = 0;
    let text = this.doc.text;
    let n = this.doc.mentions.length;
    let si = 0;
    let ei = 0;
    let level = 0;
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
        h.push(`<span class="l${level}" mention=${starts[si].index}>`);
        si++;
      }
    }
    h.push(text.slice(from));
    while (ei++ < n) h.push("</span>");

    // TODO: output themes
    return h.join("");
  }

  static stylesheet() {
    return `
      $ {
        font: 1rem anubis, serif;
        line-height: 1.5;
        padding: 4px 8px;
      }
      $ span {
        position: relative;
        color: #0b0080;
        cursor: pointer;
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


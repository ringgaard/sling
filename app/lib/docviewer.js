// Copyright 2023 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Document viewer web component.

import {Component, stylesheet} from "./component.js";
import {store, frame} from "./global.js";
import {Frame, QString} from "./frame.js";
import {Document} from "./document.js";
import {value_text, LabelCollector} from "./datatype.js";

const n_is = store.is;

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
    this.attach(this.onclick, "click");
  }

  onclick(e) {
    e.stopPropagation(); // needed?
    let ref = e.target.getAttribute("ref");
    if (ref) {
      this.dispatch("docnav", ref, true);
    } else if (e.target.className == "annotate") {
      this.dispatch("annotate", this.state, true);
    }
  }

  render() {
    let mention = this.state;
    let annotation = mention.annotation;
    let h = new Array();
    let phrase = mention.text(true);
    h.push(`<div class="phrase">${Component.escape(phrase)}</div>`);
    h.push(`<div class="annotate">&#8853;</div>`);
    if (typeof(annotation) === 'string') {
      let url = annotation;
      let anchor = Component.escape(url);
      h.push(`<div class="url"><a href="${url}">${anchor}</a></div>`);
    } else if (annotation instanceof Frame) {
      if (annotation.isanonymous()) {
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
      $ .phrase {
        font-style: italic;
        padding: 4px 0px;
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
      $ .link:hover {
        text-decoration: underline;
      }
      $ .annotate {
        position: absolute;
        top: 0;
        right: 0;
        padding: 8px;
        font-size: 20px;
        cursor: pointer;
      }
    `;
  }
};

Component.register(AnnotationBox);

export class DocumentViewer extends Component {
  onconnected() {
    this.attach(this.onmouse, "mouseover");
    this.attach(this.onleave, "mouseleave");
  }

  async onmouse(e) {
    // Ignore if selecting text or ctrl or shift is down.
    var selection = window.getSelection();
    if (selection && selection.type === 'Range') return;
    if (e.ctrlKey || e.shiftKey) return;

    // Close existing popup.
    let span = e.target;
    if (this.popup && !this.popup.contains(span)) {
      this.clear_popup();
    }

    // Find first enclosing span.
    while (span != this) {
      if (span.tagName === 'SPAN') break;
      span = span.parentElement;
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
    if (!this.state) return;

    // Parse LEX document.
    this.doc = new Document(store);
    this.doc.parse(this.state);

    // Spans sorted by begin and end positions.
    let starts = this.doc.mentions.slice();
    let ends = this.doc.mentions.slice();
    starts.sort((a, b) => a.begin - b.begin || b.end - a.end);
    ends.sort((a, b) => a.end - b.end || b.begin - a.begin);

    // Generate HTML with spans.
    let h = new Array();
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


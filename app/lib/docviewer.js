// Copyright 2023 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Document viewer web component.

import {Component} from "./component.js";
import {MdDialog} from "./material.js";
import {store} from "./global.js";
import {Document} from "./document.js";

class AnnotationBox extends MdDialog {
  onconnected() {
    this.attach(this.cancel, "click");
  }

  render() {
    let mention = this.state;
    let annotation = mention.annotation;
    if (typeof(annotation) === 'string') {
      let url = annotation;
      return `
        <md-icon-button id=close icon="close"></md-icon-button>
        <div id="content">
          <div>"${Component.escape(mention.text(true))}"</div>
          <div><a href="${url}">${Component.escape(url)}</a></div>
        </div>
      `;
    } else {
      return `
        <md-icon-button id=close icon="close"></md-icon-button>
        <div id="content">
          <div>"${Component.escape(mention.text(true))}"</div>
          <code>
            ${annotation && Component.escape(annotation.text())}
          </code>
        </div>
      `;
    }
  }

  static stylesheet() {
    return `
      $ {
        padding: 1px;
      }
      $ #content {
        padding: 16px 32px 16px 16px;
      }
      $ #close {
        position: absolute;
        right: 0;
      }
    `;
  }

};

Component.register(AnnotationBox);

export class DocumentViewer extends Component {
  onconnected() {
    this.attach(this.onclick, "click");
  }

  onclick(e) {
    let span = e.target;
    let mid = span.getAttribute("mention");
    if (!mid) return;
    let mention = this.doc.mentions[parseInt(mid)];
    this.onmention && this.onmention(span, mention);
  }

  async onmention(span, mention) {
    let box = new AnnotationBox(mention);
    let result = await box.show();
    if (result) console.log("result", result);
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
        font: 18px georgia, times, serif;
        padding: 4px 8px;
      }
      $ span {
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


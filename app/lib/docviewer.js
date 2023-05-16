// Copyright 2023 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Document viewer web component.

import {Component} from "./component.js";
import {store} from "./global.js";
import {Document} from "./document.js";

export class DocumentViewer extends Component {
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
    for (let pos = 0; pos < text.length; ++pos) {
      // Output span ends.
      while (ei < n && ends[ei].end < pos) ei++;
      while (ei < n && ends[ei].end == pos) {
        if (from < pos) {
          h.push(text.slice(from, pos));
          from = pos;
        }
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
        h.push(`<span mention=${starts[si].index}>`);
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
    `;
  }
};

Component.register(DocumentViewer);


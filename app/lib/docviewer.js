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
    this.doc = new Document(store);
    this.doc.parse(this.state);
    return this.doc.text;
  }

  static stylesheet() {
    return `
      $ {
        font: 18px georgia, times, serif;
        padding: 4px 8px;
      }
    `;
  }
};

Component.register(DocumentViewer);


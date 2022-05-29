// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Corpus browser app.

import {Component} from "/common/lib/component.js";
import {MdApp} from "/common/lib/material.js";
import {Document} from "/common/lib/docview.js";

class CorpusApp extends MdApp {
  display(url) {
    fetch(url)
      .then(response => {
        if (!response.ok) throw new Error(response.statusText);
        return response;
      })
      .then(data => data.json())
      .then(data => {
        let doc = new Document(data);
        this.find("document-viewer").update(doc);
        this.find("corpus-navigation").setDocId(doc.key);
      })
      .catch((error) => {
        console.log('Fetch error:', error);
      });
  }
}

Component.register(CorpusApp);

class CorpusNavigation extends Component {
  onconnected() {
    this.bind("#docid", "change", e => this.onchange(e));
    this.bind("#back", "click", e => this.onback(e));
    this.bind("#forward", "click", e => this.onforward(e));
  }

  onchange(e) {
    var docid = e.target.value
    if (docid) {
      let url = "/fetch?docid=" + encodeURIComponent(docid) + "&fmt=cjson";
      this.match("#app").display(url);
    }
  }

  onforward(e) {
    this.match("#app").display("/forward?fmt=cjson");
  }

  onback(e) {
    this.match("#app").display("/back?fmt=cjson");
  }

  setDocId(docid) {
    this.find("#docid").value = docid;
  }

  render() {
    return `
      <md-input id="docid" type="search" placeholder="Document ID"></md-input>
      <md-icon-button id="back" icon="arrow_back"></md-icon-button>
      <md-icon-button id="forward" icon="arrow_forward"></md-icon-button>
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        width: 400px;
      }
    `;
  }
}

Component.register(CorpusNavigation);


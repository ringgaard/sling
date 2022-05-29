// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Document analyzer app.

import {Component} from "/common/lib/component.js";
import {MdApp} from "/common/lib/material.js";
import {Document} from "/common/lib/docview.js";

class DocumentEditor extends Component {
  render() {
    return `<textarea autofocus>${Component.escape(this.state)}</textarea>`;
  }

  onupdated() {
    this.find("textarea").focus();
  }

  text() {
    return this.find("textarea").value;
  }

  static stylesheet() {
    return `
      $ textarea {
        width: 100%;
        height: 100%;
        box-sizing: border-box;
        padding: 10px;
        resize: none;
        border: 2px solid;
      }
    `;
  }
}

Component.register(DocumentEditor);

class AnalyzerApp extends MdApp {
  constructor() {
    super();
    this.document = null;
    this.text = null;
  }

  onconnected() {
    let action = this.find("#action");
    action.bind("#edit", "click", e => this.onedit(e));
    action.bind("#analyze", "click", e => this.onanalyze(e));
  }

  onanalyze(e) {
    let main = this.find("#main");
    this.text = this.find("#editor").text();

    let headers = new Headers({
      "Content-Type": "text/lex",
    });
    fetch("/annotate?fmt=cjson", {method: "POST", body: this.text, headers})
      .then(response => {
        if (response.ok) {
          return response.json();
        } else {
          console.log("annotation error", response.status, response.message);
          return null;
        }
      })
      .then(response => {
        this.document = new Document(response);
        main.update("#viewer", this.document);
        this.find("#action").update("#edit");
      });
  }

  onedit(e) {
    let main = this.find("#main");
    main.update("#editor", this.text);
    this.find("#action").update("#analyze");
  }

  static stylesheet() {
    return `
      $ md-content {
        overflow: hidden;
      }
    `;
  }
}

Component.register(AnalyzerApp);


// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {MdApp, MdCard} from "/common/lib/material.js";

class WitexApp extends MdApp {
  onconnected() {
    this.bind("#wikiurl", "keyup", e => {
      if (e.key === "Enter") this.onfetch();
    });
  }

  async onfetch() {
    let url = this.find("#wikiurl").value();
    console.log("fetch", url);
    this.style.cursor = "wait";
    let r = await fetch(`/witex/extract?url=${url}`);
    let page = await r.json();
    this.style.cursor = "";
    this.find("#ast").update(page);
  }

  static stylesheet() {
    return `
      $ md-input {
        display: flex;
        max-width: 600px;
      }
      $ #title {
        padding-right: 16px;
      }
    `;
  }
}

Component.register(WitexApp);

class WikiAst extends MdCard {
  visible() { return this.state; }

  render() {
    let page = this.state;
    if (!page) return;
    return `
      <md-card-toolbar>
        <div>AST</div>
      </md-card-toolbar>
      <pre>${page.ast}</pre>
    `;
  }

  static stylesheet() {
    return `
      $ {
        overflow-x: auto;
      }
      $ pre {
        font-size: 12px;
      }
    `;
  }
}

Component.register(WikiAst);

document.body.style = null;


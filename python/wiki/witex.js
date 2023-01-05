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
    this.find("#tables").update(page);
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

class WikiTables extends Component {
  render() {
    let page = this.state;
    if (!page) return;
    let cards = new Array();
    for (let table of page.tables) {
      let card = new WikiTable({page, table});
      cards.push(card);
    }
    return cards;
  }
}

Component.register(WikiTables);

class WikiTable extends MdCard {
  onrendered() {
    this.attach(this.onexpand, "click", "#expand");
  }

  onexpand(e) {
    this.expanded = !this.expanded;
    this.update(this.state);
  }

  render() {
    if (!this.state) return;
    let table = this.state.table;
    if (this.expanded) {
      let h = new Array();
      h.push(`
        <md-card-toolbar>
          <div>Table: ${table.title}</div>
          <md-spacer></md-spacer>
          <md-icon id="expand" icon="expand_less">
        </md-card-toolbar>`);
      h.push("<div><table>");
      for (let row of table.headers) {
        h.push("<tr>");
        h.push("<th>Skip</th>");
        for (let cell of row) {
          h.push(`<th>${cell}</th>`);
        }
        h.push("</tr>");
      }
      for (let row of table.rows) {
        h.push("<tr>");
        h.push('<td><input type="checkbox"></th>');
        for (let cell of row) {
          h.push(`<td>${cell || ""}</td>`);
        }
        h.push("</tr>");
      }
      h.push("</table></div>");
      return h.join("");
    } else {
      let labels = table.headers[0] || [];
      return `
        <md-card-toolbar>
          <div>Table: ${table.title}</div>
          <md-spacer></md-spacer>
          <md-icon id="expand" icon="expand_more">
        </md-card-toolbar>
        <div>${labels.join(" | ")} (${table.rows.length} rows)</div>`;
    }
  }

  static stylesheet() {
    return `
      $ table {
        border-collapse: collapse;
      }
      $ td, $ th {
        border: 1px solid black;
        padding: 4px;
      }
    `;
  }
}

Component.register(WikiTable);

class WikiAst extends MdCard {
  visible() { return this.state; }

  onrendered() {
    this.attach(this.onexpand, "click", "#expand");
  }

  onexpand(e) {
    this.expanded = !this.expanded;
    this.update(this.state);
  }

  render() {
    let page = this.state;
    if (!page) return;
    return `
      <md-card-toolbar>
        <div>AST</div>
        <md-spacer></md-spacer>
        <md-icon id="expand" icon="expand_${this.expanded ? "less" : "more"}">
      </md-card-toolbar>
      <pre>${this.expanded ? page.ast : ""}</pre>
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


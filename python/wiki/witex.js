// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {MdApp, MdCard} from "/common/lib/material.js";

const lib = {
  "text": () => this,
  "link": () => this,
  "id": () => this,
  "int": () => this ? parseInt(this) : undefined,
};

class Script {
  constructor(script) {
    this.pipe = new Array();

    let code;
    let colon = script.indexOf(':');
    if (colon != -1) {
      code = script.slice(colon + 1);
      script = script.slice(0, colon);
    }

    for (let expr of script.split('|')) {
      expr = expr.trim();
      if (this.pipe.length == 0) {
        let num = parseInt(expr);
        if (!isNaN(num)) expr = num;
        if (expr == "*") expr = null;
        this.pipe.push(expr);
      } else {
        this.pipe.push(lib[expr]);
      }
    }

    if (code) {
      let func = new Function("value", "record", code);
      this.pipe.push(func);
    }
  }

  execute(record) {
    let value = this.pipe[0];
    if (value === null) {
      value = record;
    } else {
      value = record.field(value);
    }
    for (let i = 1; i < this.pipe.length; ++i) {
      value = this.pipe[i].call(row, value);
    }
    return value;
  }
}

function parse_template(template) {
  let pos = 0;
  let start = 0;
  let size = template.length;
  let parts = new Array();
  while (pos < size) {
    let ch = template.charCodeAt(pos);
    if (ch == 0x3C) {
      if (start < pos) {
        parts.push(template.slice(start, pos));
      }
      start = ++pos;
      while (pos < size) {
        let ch = template.charCodeAt(pos);
        if (ch == 0x3E) break;
        pos++;
      }
      parts.push(new Script(template.slice(start, pos)));
      start = ++pos;
    } else {
      pos++;
    }
  }
  if (start < pos) {
    parts.push(template.slice(start, pos));
  }
  return parts;
}

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
    if (this.expanded) {
      this.attach(this.onrowclick, "click", "table");
      this.attach(this.onextract, "click", "#extract");
    }
  }

  onexpand(e) {
    this.expanded = !this.expanded;
    this.update(this.state);
  }

  onrowclick(e) {
    let tr = e.target.closest("tr");
    if (tr) {
      if (tr.className == "skip") {
        tr.className = "header";
      } else if (tr.className == "header") {
        tr.className = "";
      } else {
        tr.className = "skip";
      }
    }
  }

  onextract(e) {
    let table = this.state.table;
    let template = this.find("#template").value;
    let columns = new Map();
    let skipped = new Set();
    for (let r of this.querySelectorAll("tr").values()) {
      let rowno = parseInt(r.getAttribute("rowno"));
      let row = table.rows[rowno];
      if (r.className == "header") {
        for (let c = 0; c < row.length; ++c) {
          let colname = row[c];
          if (!columns.has(colname)) columns.set(colname, c);
          skipped.add(rowno);
        }
      } else if (r.className == "skip") {
        skipped.add(rowno);
      }
    }

    let parts = parse_template(template);
    console.log("extract parts", parts);
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
      h.push("<div>");

      h.push("<table>");
      let rowno = 0;
      for (let row of table.rows) {
        h.push(`<tr rowno=${rowno}${rowno == 0 ? ' class="header"' : ''}>`);
        for (let cell of row) {
          h.push(`<td>${cell || ""}</td>`);
        }
        h.push("</tr>");
        rowno++;
      }
      h.push("</table>");

      h.push(`<textarea
                 id="template"
                 rows="20"
                 placeholder="Extraction template"></textarea>`);
      h.push(`<button id="extract">Extract</button>`);

      h.push("</div>");
      return h.join("");
    } else {
      let columns = Object.keys(table.columns);
      return `
        <md-card-toolbar>
          <div>Table: ${table.title}</div>
          <md-spacer></md-spacer>
          <md-icon id="expand" icon="expand_more">
        </md-card-toolbar>
        <div>${columns.join(" | ")} (${table.rows.length} rows)</div>`;
    }
  }

  static stylesheet() {
    return `
      $ table {
        border-collapse: collapse;
      }
      $ td {
        border: 1px solid black;
        padding: 4px;
      }
      $ tr.header {
        font-weight: bold;
      }
      $ tr.skip {
        text-decoration: line-through;
      }
      $ textarea {
        display: block;
        width: 95%;
        margin: 10px 10px 10px 0px;
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


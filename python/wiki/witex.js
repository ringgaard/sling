// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Store, Reader, Frame} from "/common/lib/frame.js";
import {Component} from "/common/lib/component.js";
import {MdApp, MdCard, inform} from "/common/lib/material.js";

var commons = new Store();
const n_is = commons.lookup("is");
const n_isa = commons.lookup("isa");
const n_amount = commons.lookup("/w/amount");
const n_unit = commons.lookup("/w/unit");
const n_years_old = commons.lookup("Q24564698");
const n_time = commons.lookup("/w/time");

const empty_values = new Set(["-", "--", "—", "?"]);

var last_template = "";

const lib = {
  "age": function(value) {
    if (value instanceof Document) value = value.text;
    let age = value && parseInt(value);
    if (!age) return;
    let frame = this.store.frame();
    frame.add(n_amount, age);
    frame.add(n_unit, n_years_old);
    return frame;
  },

  "date": (value) => {
    if (!value) return;
    if (value instanceof Document) {
      let a = value.annotation(0);
      if (a && a.get(n_isa) == n_time) return value;
      value = value.text;
    }
    let dateval = value.toString();
    let d = new Date(dateval);
    if (d.getMonth() == 0 && d.getDate() == 1) {
      return d.getFullYear();
    } else {
      return d.getFullYear() * 10000 + (d.getMonth() + 1) * 100 + d.getDate();
    }
  },

  "article": (value) => value.article(),

  "id": (value) => value && value.id,

  "int": (value) => {
    if (value instanceof Document) value = value.text;
    return value && parseInt(value);
  },

  "link": (value) => {
    if (value instanceof Document) return value.annotation(0);
  },


  "memoize": function(value) {
    if (value instanceof Document) {
      if (!this.links) this.links = new Map();
      let link = value.mentions[0] && value.mentions[0].annotation;
      if (link) {
        this.links.set(value.text, link);
        value = link;
      } else {
        value = this.links.get(value.text) || value.text;
      }
    }
    return value;
  },

  "phrase": value => {
    if (value instanceof Document) {
      return value.phrase(0) || value;
    } else {
      return value;
    }
  },

  "text": (value) => {
    if (value instanceof Document) return value.text;
  },
};

class Script {
  constructor(script, columns) {
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
        let num = columns.get(expr);
        if (num === undefined) {
          num = parseInt(expr);
          if (!isNaN(num)) expr = num;
        }
        if (expr == "*") expr = null;
        this.pipe.push(expr);
      } else {
        this.pipe.push(lib[expr]);
      }
    }

    if (code) {
      let func = new Function("value", code);
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
      value = this.pipe[i].call(record, value);
    }
    return value;
  }
}

function parse_template(template, columns) {
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
      parts.push(new Script(template.slice(start, pos), columns));
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

function detag(html) {
  return html.replace(/<\/?[^>]+(>|$)/g, "");
}

class Mention {
  constructor(begin, end, annotation) {
    this.begin = begin;
    this.end = end;
    this.annotation = annotation;
  }
}

class Document {
  constructor(store, text) {
    this.store = store;
    this.text = text;
    this.mentions = new Array();
    this.themes = new Array();
  }

  mention(idx) {
    return this.mentions[idx];
  }

  annotation(idx) {
    let mention = this.mentions[idx];
    return mention && mention.annotation;
  }

  phrase(idx) {
    let mention = this.mentions[idx];
    if (!mention) return null;
    return this.text.slice(mention.begin, mention.end);
  }
}

function delex(document, lex) {
  let text = "";
  let stack = new Array();
  let level = 0;
  let pos = 0;
  while (pos < lex.length) {
    let c = lex[pos++];
    switch (c) {
      case "[":
        stack.push(text.length);
        break;

      case "]": {
        let begin = stack.pop();
        let end = pos;
        document.mentions.push(new Mention(begin, end));
        break;
      }

      case "|": {
        if (stack.length == 0) {
          text += c;
        } else {
          let nesting = 0;
          let start = pos;
          while (pos < lex.length) {
            let c = lex[pos++];
            if (c == '{') {
              nesting++;
            } else if (c == '}') {
              nesting--;
            } else if (c == ']') {
              if (nesting == 0) break;
            }
          }
          let reader = new Reader(document.store, lex.slice(start, pos - 1));
          let obj = reader.parseAll();
          document.mentions.push(new Mention(stack.pop(), text.length, obj));
        }
        break;
      }

      case "{": {
        let nesting = 0;
        let start = pos;
        while (pos < lex.length) {
          let c = lex[pos++];
          if (c == '{') {
            nesting++;
          } else if (c == '}') {
            if (--nesting == 0) break;
          }
        }
        let reader = new Reader(document.store, lex.slice(start - 1, pos));
        let obj = reader.parseAll();
        document.themes.push(obj);
        break;
      }

      default:
        text += c;
    }
  }
  document.text = text;
}

class Record {
  constructor(store, page, table) {
    this.store = store;
    this.page = page;
    this.table = table;
    this.colnames = new Map();
    this.fields = null;
  }

  add_column_name(colname, colno) {
    if (!this.colnames.has(colname)) {
      this.colnames.set(colname, colno);
    }
  }

  field(col) {
    if (typeof(col) !== "number") col = this.colnames.get(col);
    return this.fields[col];
  }

  article() {
    if (this.page.qid) return this.store.lookup(this.page.qid);
    return this.page.title;
  }
}

class WitexApp extends MdApp {
  onconnected() {
    this.bind("#wikiurl", "keyup", e => {
      if (e.key === "Enter") this.onfetch();
    });
  }

  async onfetch() {
    let url = this.find("#wikiurl").value();
    this.style.cursor = "wait";
    let r = await fetch(`/witex/extract?url=${url}`);
    this.style.cursor = "";
    if (!r.ok) {
      let message = await r.text();
      inform(`Error ${r.status}: ${message}`);
    } else {
      let page = await r.json();
      this.find("#tables").update(page);
      this.find("#ast").update(page);
    }
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
      this.attach(this.onrepeat, "click", "#repeat");
    }
  }

  onexpand(e) {
    this.expanded = !this.expanded;
    this.update(this.state);
  }

  onrowclick(e) {
    if (e.target.className != "rowid") return;
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

  onrepeat(e) {
    this.find("#template").value = last_template;
  }

  onextract(e) {
    let store = new Store(commons);
    let page = this.state.page;
    let table = this.state.table;
    let record = new Record(store, page, table);
    let colnames = record.colnames;

    // Find column names and skipped rows.
    let skipped = new Set();
    for (let r of this.querySelectorAll("tr").values()) {
      let rowno = parseInt(r.getAttribute("rowno"));
      let row = table.rows[rowno];
      if (r.className == "header") {
        for (let c = 0; c < row.length; ++c) {
          let colname = row[c].replace(/\s+/g, " ").trim();
          record.add_column_name(colname, c);
          skipped.add(rowno);
        }
      } else if (r.className == "skip") {
        skipped.add(rowno);
      }
    }

    // Parse extraction template.
    let template = this.find("#template").value;
    let parts = parse_template(template, colnames);

    // Extract data from each row in the table.
    let extractions = new Array();
    for (let rowno = 0; rowno < table.rows.length; ++rowno) {
      if (skipped.has(rowno)) continue;

      // Get fields for row.
      let row = table.rows[rowno];
      record.fields = new Array();
      for (let cell of row) {
        if (empty_values.has(cell)) cell = null;
        if (!cell) {
          record.fields.push(null);
        } else {
          let doc = new Document(store);
          delex(doc, detag(cell));
          record.fields.push(doc);
        }
      }

      // Extract data from row using template.
      let result = "";
      for (let part of parts) {
        let piece = part;
        if (piece instanceof Script) {
          piece = piece.execute(record)
          if (!piece) {
            result += "null";
          } else if (piece instanceof Document) {
            let annotation = piece.annotation(0);
            if (annotation instanceof Frame) {
              if (annotation.get(n_isa) == n_time) {
                result += JSON.stringify(annotation.get(n_is));
              } else {
                result += annotation.id || annotation.text(false, true);
              }
            } else {
              let phrase = piece.phrase(0);
              result += JSON.stringify(phrase || piece.text)
            }
          } else if (piece instanceof Frame) {
            result += piece.id || piece.text(false, true);
          } else {
            result += JSON.stringify(piece);
          }
        } else {
          result += piece;
        }
      }

      // Remove empty slots.
      let frame = store.parse(result);
      frame.apply((name, value) => {
        if ((value instanceof Frame) && value.isanonymous()) {
          if (value.has(n_is) && !value.get(n_is)) {
            value = null;
          } else {
            value.purge((n, v) => !v);
            if (value.length == 0) value = null;
          }
          return [name, value];
        }
      });
      frame.purge((name, value) => !value);
      result = frame.text(false, true);

      extractions.push(result);
    }

    let output = this.find("#output");
    output.innerText = extractions.join("\n");
    window.getSelection().selectAllChildren(output);
    last_template = template;
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
        h.push(`<td class="rowid">${rowno || "Row#"}</td>`);
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
      h.push(`<button id="repeat">Repeat</button>`);

      h.push(`<pre id="output"></pre>`);

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
      $ td.rowid {
        cursor: pointer;
        text-align: right;
      }
      $ tr.header {
        font-weight: bold;
        background-color: lightgrey;
      }
      $ tr.skip {
        text-decoration: line-through;
        color: grey;
      }
      $ textarea {
        display: block;
        width: 95%;
        margin: 10px 10px 10px 0px;
      }

      $ pre {
        overflow: hidden;
        white-space: pre-wrap;
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


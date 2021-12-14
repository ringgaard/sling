// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {MdSearchList} from "/common/lib/material.js";
import {Frame, Printer} from "/common/lib/frame.js";
import {store, settings} from "./global.js";
import {value_text, value_parser} from "./value.js";
import {search} from "./omnibox.js";
import {psearch} from "./schema.js";

const n_id = store.id;
const n_is = store.is;
const n_name = store.lookup("name");
const n_media = store.lookup("media");
const n_target = store.lookup("target");
const n_item_type = store.lookup("/w/item");
const n_quantity_type = store.lookup("/w/quantity");

function qualified(v) {
  if (v instanceof Frame) {
    return v.has(n_is) && !v.has(n_id);
  } else {
    return false;
  }
}

class FactEditor extends Component {
  oninit() {
    this.setAttribute("contenteditable", true);
    this.setAttribute("spellcheck", false);
    this.focus();

    this.bind(null, "keydown", e => this.onkeydown(e));
    this.bind(null, "click", e => this.onclick(e));
    this.bind(null, "copy", e => this.oncopy(e));
    this.bind(null, "paste", e => this.onpaste(e));

    this.bind(null, "input", e => this.oninput(e));
    this.bind(null, "focusin", e => this.onfocusin(e));
    this.bind(null, "focusout", e => this.onfocusout(e));
  }

  onfocusin(e) {
    //console.log("focusin", this.value, e);
  }

  onfocusout(e) {
    //console.log("focusout", this.value, e);
  }

  onselection(focus, anchor) {
    if (this.focused && this.focused != focus.field) {
      this.focused.collapse();
      this.focused = null;
    }
  }

  oninput(e) {
    let s = this.selection();
    if (!s) return;
    if (s.field instanceof FactField) {
      if (this.focused && this.focused != s.field) this.focused.collapse();
      s.field.onchanged(e);
      this.focused = s.field;
    }
  }

  onmove(e, down) {
    let s = this.selection();
    if (!s) return;

    let next = down ? s.statement.nextSibling : s.statement.previousSibling;
    if (!next) return;

    let focus = next.firstChild || next;
    if (!focus) return;

    let anchor = e.shiftKey ? s.anchor : focus;
    let anchorofs = e.shiftKey ? s.anchorofs : 0;
    s.selection.setBaseAndExtent(anchor, anchorofs, focus, 0);
    e.preventDefault();
  }

  ontab(e) {
    e.preventDefault();
    let forward = !e.shiftKey;
    let s = this.selection();
    if (!s) return;

    let focus = forward ? s.field.nextSibling : s.field.previousSibling;
    if (!focus) {
      if (forward) {
        let next = s.statement.nextSibling;
        if (!next) return;
        focus = next.firstChild;
      } else {
        let next = s.statement.previousSibling;
        if (!next) return;
        focus = next.lastChild;
      }
    }
    if (!focus) return;

    s.selection.setBaseAndExtent(focus, 0, focus, 0);
  }

  onhome(e) {
    e.preventDefault();
    let s = this.selection();
    if (!s) return;
    let home = s.property;
    if (!home) return;

    let anchor = e.shiftKey ? s.anchor : home;
    let anchorofs = e.shiftKey ? s.anchorofs : 0;
    s.selection.setBaseAndExtent(anchor, anchorofs, home, 0);
  }

  onend(e) {
    e.preventDefault();
    let s = this.selection();
    if (!s) return;
    let end = s.value;
    if (!end) return;

    let anchor = e.shiftKey ? s.anchor : end;
    let anchorofs = e.shiftKey ? s.anchorofs : 1;
    s.selection.setBaseAndExtent(anchor, anchorofs, end, 1);
  }

  onspace(e) {
    let s = this.selection();
    if (s && s.field == s.property && s.position == 0) {
      s.statement.qualified = true;
      e.preventDefault();
    }
  }

  onbackspace(e) {
    let s = this.selection();
    if (!s) return;
    console.log("bs", s);
    if (s.position == 0) {
      if (s.field == s.property) s.statement.qualified = false;
      if (s.selection.isCollapsed) e.preventDefault();
    }
    if (s.position == 1) {
      e.preventDefault();
      s.field.input.innerHTML = "";
      if (s.field instanceof FactField) s.field.onchanged(e);
    }
  }

  ondelete(e) {
    let s = this.selection();
    console.log("del", s);
  }

  searching() {
    return this.focused &&
           (this.focused instanceof FactField) &&
           this.focused.searching();
  }

  onkeydown(e) {
    if (e.key === "ArrowUp") {
      if (this.searching()) {
        this.focused.prev();
        e.preventDefault();
      } else {
        this.onmove(e, false);
      }
    } else if (e.key === "ArrowDown") {
      if (this.searching()) {
        this.focused.next();
        e.preventDefault();
      } else {
        this.onmove(e, true);
      }
    } else if (e.key === "Tab") {
      this.ontab(e);
    } else if (e.key === "Home") {
      this.onhome(e);
    } else if (e.key === "End") {
      this.onend(e);
    } else if (e.key === "Backspace") {
      this.onbackspace(e);
    } else if (e.key === "Delete") {
      this.ondelete(e);
    } else if (e.key === "Enter") {
      if (this.searching()) {
        this.focused.onenter(e);
      }
    } else if (e.key === "Escape") {
      if (this.searching()) {
        this.focused.collapse();
        e.stopPropagation();
      }
    } else if (e.key == " ") {
      this.onspace(e);
    }
  }

  onclick(e) {
    // Prevent topic selection.
    e.stopPropagation();
  }

  onupdated() {
    this.focus();
  }

  oncopy(e) {
    let s = this.selection();
    console.log(s.selection);
  }

  onpaste(e) {
    // Get HTML from clipboard.
    let html = e.clipboardData.getData('text/html');
    let clip = document.createElement("div");
    clip.innerHTML = html;

    let styled = clip.querySelectorAll("[style]");
    for (let i = 0; i < styled.length; ++i) {
      styled[i].removeAttribute("style");
    }
    console.log("clip", clip);
  }

  selection() {
    let s = document.getSelection();
    if (!s.focusNode) return;

    let field = s.focusNode;
    if (!field) return;
    if (field.nodeType == Node.TEXT_NODE) field = field.parentElement;
    if (field instanceof FactInput) field = field.parentElement;

    let base = s.anchorNode;
    if (base && base.nodeType == Node.TEXT_NODE) base = field.parentElement;
    if (base && (base instanceof FactInput)) base = base.parentElement;

    let statement = field.closest("fact-statement");
    if (!statement) return;

    let property = statement.firstChild;
    let value = statement.lastChild;
    let position = s.focusOffset;

    return {
      statement, property, value, field, position, base,
      selection: s,
      anchor: s.anchorNode,
      anchorofs: s.anchorOffset,
    };
  }

  slots() {
    let topic = this.state;
    let s = new Array();
    for (let id of topic.all(n_id)) {
      s.push(n_id);
      s.push(id);
    }
    for(let e = this.firstChild; e; e = e.nextSibling) {
      if (e instanceof FactStatement) {
        let prop = e.firstChild;
        let value = e.lastChild;
        if (!prop || !value) continue;
        let p = prop.value();
        let v = value.value();
        if (e.qualified) {
          if (s.length == 0) return;
          let prev = s[s.length - 1];
          if (!qualified(prev)) {
            let q = store.frame();
            q.add(n_is, prev);
            s[s.length - 1] = q;
            prev = q;
          }
          prev.add(p, v);
        } else {
          s.push(p);
          s.push(v);
        }
      }
    }
    for (let id of topic.all(n_media)) {
      s.push(n_media);
      s.push(id);
    }
    return s;
  }

  render() {
    let topic = this.state;
    if (!topic) {
      this.style.display = "none";
      return "";
    } else {
      this.style.display = "";
    }

    let h = new Array();
    for (let i = 0; i < topic.length; ++i) {
      let prop = topic.name(i);
      if (prop == n_id || prop == n_media) continue;
      let value = topic.value(i);
      if (qualified(value)) {
        h.push(new FactStatement({property: prop, value: value.get(n_is)}));
        for (let j = 0; j < value.length; ++j) {
          let qprop = value.name(j);
          if (qprop == n_is) continue;
          let qval = value.value(j);
          h.push(new FactStatement({
            property: qprop,
            value: qval,
            qualified: true,
          }));
        }
      } else {
        h.push(new FactStatement({property: prop, value: value}));
      }
    }
    h.push(new FactStatement());
    return h;
  }

  static stylesheet() {
    return `
      $ {
        display: block;
        font-size: 16px;
        margin-top: 8px;
        padding-left: 4px;
        padding-bottom: 4px;
        border: thin solid lightgrey;
        outline: none;
      }
    `;
  }
}

Component.register(FactEditor);

class FactStatement extends Component {
  constructor(state) {
    super(state);
    if (state) {
      this.qualified = state.qualified;
    }
  }

  get qualified() {
    return this.state && this.state.qualified;
  }

  set qualified(v) {
    if (this.state) this.state.qualified = v;
    this.className = v ? "qualified" : "";
  }

  render() {
    let statement = this.state;
    if (!statement) return "";
    return [
      new FactProperty({property: n_item_type, value: statement.property}),
      new FactValue({property: statement.property, value: statement.value}),
    ];
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        padding-top: 4px;
        min-height: 1.25em;
      }
      $.qualified {
        font-size: 13px;
        margin-left: 32px;
      }
    `;
  }
}

Component.register(FactStatement);

class FactInput extends Component {
  static stylesheet() {
    return `
      $ {
        display: inline-block; /* prevent deletion */
        min-height: 1.25em;
      }
    `;
  }
}

Component.register(FactInput);

class FactField extends Component {
  onconnected() {
    this.input = this.find("fact-input");
    this.list = this.find("md-search-list");
  }

  async onchanged(e) {
    let text = this.text();
    if (text != this.getAttribute("text")) {
      this.removeAttribute("value");
      this.removeAttribute("text");
      this.className = "";

      let results = await search(this.text(), this.backends());
      this.list.update({items: results});
    }
  }

  onenter(e) {
    e.preventDefault();
    let item = this.list.active.state;
    this.select(item);
  }

  next() {
    this.list.next();
  }

  prev() {
    this.list.prev();
  }

  searching() {
    let items = this.list.state && this.list.state.items;
    return items && items.length > 0;
  }

  text() {
    return this.input.innerText.trim();
  }

  value() {
    if (this.hasAttribute("value")) {
      let value = this.getAttribute("value");
      return store.parse(value);
    } else {
      let text = this.text();
      if (this.state.property.get(n_target) == n_quantity_type) {
        let number = parseFloat(text);
        if (!isNaN(number)) return number;
      }
      return text;
    }
  }

  async select(item) {
    var value, text;
    let encoded = true;
    if (item.onitem) {
      // Run plug-in and use new topic as value.
      await item.onitem(item);
      if (item.context && item.context.added) {
        item.context.select = false;
        await item.context.refresh();
        value = item.context.added;
      }
    } else if (item.topic) {
      if (item.casefile) {
        // Create new topic with reference to topic in external case.
        let editor = this.match("#editor");
        let link = this.new_topic();
        link.add(n_is, topic);
        let name = topic.get(n_name);
        if (name) link.add(n_name, name);
        await editor.update_list();
        value = link;
        text = name;
      } else {
        value = item.topic.id
        text = item.topic.get(n_name);
      }
    } else if (item.value) {
      if (item.value instanceof Frame) {
        value = item.value.id || item.value.text(false, true);
        [text, encoded] = value_text(item.value, this.state.property);
      } else {
        let printer = new Printer(store);
        printer.refs = null;
        printer.print(item.value);
        value = printer.output;
        [text, encoded] = value_text(item.value, this.state.property);
      }
    } else if (item.ref) {
      value = item.ref;
      text = item.name;
    }

    this.collapse();
    this.setAttribute("value", value);
    this.setAttribute("text", this.text());
    this.className = encoded ? "encoded" : "";
    this.input.innerHTML = Component.escape(text);

    // Move cursor to end of field.
    let range = document.createRange();
    range.selectNodeContents(this.input);
    range.collapse(false);
    let selection = window.getSelection();
    selection.removeAllRanges();
    selection.addRange(range);
  }

  collapse() {
    this.list.update();
  }

  render() {
    if (!this.state) return;
    let val = this.state.value;
    let prop = this.state.property;
    let [text, encoded] = value_text(val, prop);
    if (encoded) {
      let value = val.id;
      if (!value) {
        if (val instanceof Frame) {
          value = val.text(false, true);
        } else {
          value = val;
        }
      }
      this.setAttribute("value", value);
      this.setAttribute("text", text);
      this.className = "encoded";
    } else {
      this.removeAttribute("value");
      this.removeAttribute("text");
      this.className = "";
    }

    return `
      <fact-input>${Component.escape(text)}</fact-input>
      <md-search-list></md-search-list>
    `;
  }
}

Component.register(FactField);

class FactProperty extends FactField {
  backends() {
    return [psearch];
  }

  static stylesheet() {
    return `
      $ {
        display: block;
        font-weight: 500;
        margin-right: 4px;
        white-space: nowrap;
      }
      $ fact-input::after {
        content: ":";
      }
      $.encoded {
        color: #0b0080;
      }
      .qualified $ {
        font-weight: normal;
      }
      $ md-search-list {
        color: black;
        font-size: 15px;
        width: 400px;
      }
    `;
  }
}

Component.register(FactProperty);

class FactValue extends FactField {
  backends() {
    let editor = this.match("#editor");
    return [
      (query, full, results) => value_parser(query, results),
      (query, full, results) => editor.search(query, full, results),
    ];
  }

  static stylesheet() {
    return `
      $ {
        display: block;
      }
      $.encoded {
        color: #0b0080;
      }
      $ md-search-list {
        color: black;
        font-size: 15px;
        width: 400px;
      }
    `;
  }
}

Component.register(FactValue);

function closest_fact(n) {
  while (n) {
    if (n instanceof FactField) {
      return {field: n, statement: n.parentElement};
    }
    if (n instanceof FactStatement) {
      return {statement: n};
    }
    n = n.parentNode;
  }
}

// Mark selected topics when selection changes.
document.addEventListener("selectionchange", () => {
  // Get current selection.
  let selection = document.getSelection();

  let focus = closest_fact(selection.focusNode);
  if (!focus) return;
  let anchor = closest_fact(selection.anchorNode);
  if (!anchor) return;

  // Notify fact editor about selection change.
  let editor = focus.statement.closest("fact-editor");
  if (editor) editor.onselection(focus, anchor);

  // Ignore (collapsed) selection within fact field.
  if (selection.isCollapsed) return;
  if (focus.field == anchor.field) return;

  // Determine selection direction.
  let forward = false;
  let e = anchor.statement == focus.statement ? anchor.field : anchor.statement;
  while (e) {
    if (e == focus.statement || e == focus.field) {
      forward = true;
      break;
    }
    e = e.nextSibling;
  }

  // Expand selection to cover full fact statements.
  if (forward && (focus.field instanceof FactValue)) {
    focus.statement = focus.statement.nextSibling;
  }
  if (!forward && (anchor.field instanceof FactValue)) {
    anchor.statement = anchor.statement.nextSibling;
  }

  selection.setBaseAndExtent(anchor.statement, 0, focus.statement, 0);
});


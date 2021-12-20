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

function range(a, b) {
  if (!a || !b || !a.parentNode || a.parentNode != b.parentNode) {
    return [null, null];
  }
  let e = a.parentNode.firstChild;
  while (e) {
    if (e == a) return [a, b];
    if (e == b) return [b, a];
    e = e.nextSibling;
  }
}

class FactPanel extends Component {
  editor() {
    return this.find("fact-editor");
  }

  list() {
    return this.find("md-search-list");
  }

  slots() {
    return this.editor().slots();
  }

  onupdated() {
    let editor = this.editor();
    if (editor) {
      editor.update(this.state);
    }
  }

  render() {
    if (!this.state) {
      this.style.display = "none";
      return "";
    } else {
      this.style.display = "";
    }

    return `
      <md-search-list></md-search-list>
      <fact-editor></fact-editor>
    `
  }

  static stylesheet() {
    return `
      $ {
        position: relative;
      }
      $ md-search-list {
        position: absolute;
        color: black;
        font-size: 15px;
        width: 600px;
      }
    `;
  }
}

Component.register(FactPanel);

class FactEditor extends Component {
  oninit() {
    this.setAttribute("contenteditable", true);
    this.setAttribute("spellcheck", false);
    this.focus();
    this.dirty = false;

    this.list = this.parentElement.list();
    this.list.bind(null, "select", e => this.onselect(e));

    this.bind(null, "keydown", e => this.onkeydown(e));
    this.bind(null, "click", e => this.onclick(e));
    this.bind(null, "cut", e => this.oncut(e));
    this.bind(null, "paste", e => this.onpaste(e));

    this.bind(null, "input", e => this.oninput(e));
    this.bind(null, "beforeinput", e => this.onbeforeinput(e));
    this.bind(null, "focusin", e => this.onfocusin(e));
    this.bind(null, "focusout", e => this.onfocusout(e));
  }

  onfocusin(e) {
    if (this.cursor) {
      // Restore current selection.
      let  selection = window.getSelection();
      selection.removeAllRanges();
      selection.addRange(this.cursor);
      this.cursor = null;
    }
  }

  onfocusout(e) {
    // Close search box.
    if (this.focused) {
      this.focused.collapse();
      this.focused = null;
    }

    // Save current selection.
    let selection = document.getSelection();
    if (selection.rangeCount == 0 || this.style.display == "none") {
      this.cursor = null;
    } else {
      this.cursor = selection.getRangeAt(0);
    }
  }

  onselection(focus, anchor) {
    if (this.focused && this.focused != focus.field) {
      this.focused.collapse();
      this.focused = null;
    }
  }

  onbeforeinput(e) {
    let selection = document.getSelection();
    let focus = selection.focusNode;
    if (!focus) return;
    if ((focus instanceof FactStatement) && focus.placeholder()){
      // Add property and value to current placeholder.
      focus.appendChild(new FactProperty({property: "", value: ""}));
      focus.appendChild(new FactValue({property: "", value: ""}));

      // Make statement qualified on space.
      if (e.data == " ") {
        focus.qualified = true;
        e.preventDefault();
      }
    } else if (selection.anchorNode != selection.focusNode) {
      let s = this.selection();
      if (s.field && s.base && s.field != s.base) {
        this.delete_selection(s);
      }
    }
  }

  oninput(e) {
    this.dirty = true;
    let s = this.selection();
    if (!s) return;
    if (s.field instanceof FactField) {
      if (this.focused && this.focused != s.field) this.focused.collapse();
      s.field.onchanged(e);
      this.focused = s.field;
    }
    if (!this.lastChild.empty()) {
      let placeholder = new FactStatement();
      this.appendChild(placeholder);
    }
  }

  onenter(e) {
    e.preventDefault();
    if (document.getSelection().isCollapsed) {
      let s = this.selection();
      let stmt = new FactStatement();
      let point = s && s.statement;
      if (point && point.qualified) {
        stmt.qualified = true;
      }
      if (point && s.field == s.value) point = point.nextSibling;
      this.insertBefore(stmt, point);
      document.getSelection().collapse(stmt, 0);
    }
  }

  onselect(e) {
    if (this.focused) this.focused.onselect(e);
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
    if (!s) return;
    if (s.field == s.property && s.position == 0) {
      if (!s.statement.qualified) {
        s.statement.qualified = true;
        this.dirty = true;
      }
      e.preventDefault();
    }
  }

  onbackspace(e) {
    let s = this.selection();
    if (!s) return;
    if (s.selection.isCollapsed && s.position == 0) {
      e.preventDefault();
      if (s.statement.qualified) {
        s.statement.qualified = false;
        this.dirty = true;
      } else {
        let prev = s.statement.previousSibling;
        if (prev && prev.empty()) {
          prev.remove();
        } else if (s.statement.empty()) {
          s.statement.remove();
        }
      }
    }
    if (s.statement && s.field != s.base) {
      e.preventDefault();
      this.delete_selection(s);
    }
  }

  ondelete(e) {
    let s = this.selection();
    if (!s) return;
    if (s.selection.isCollapsed && s.statement.empty()) {
      e.preventDefault();
      s.statement.remove();
    } else if (s.statement && s.field != s.base) {
      e.preventDefault();
      this.delete_selection(s);
    } else if (s.field && s.position == s.field.textlen()) {
      e.preventDefault();
    }
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
      if (this.searching()) {
        this.focused.ontab(e);
      } else {
        this.ontab(e);
      }
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
      } else {
        this.onenter(e);
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

  oncut(e) {
    let s = this.selection();
    if (!s) return;
    if (s.field != s.base) {
      e.preventDefault();
      document.execCommand("copy");
      this.delete_selection(s);
    }
  }

  onpaste(e) {
    // Get HTML from clipboard with fallback to plain text.
    let html = e.clipboardData.getData('text/html');
    let clip = document.createElement("div");
    if (html) {
      clip.innerHTML = html;
    } else {
      let text = e.clipboardData.getData('text/plain');
      if (!text) return;
      clip.innerText = text;
    }

    if (clip.querySelector("fact-statement")) {
      // Clear select before paste.
      let s = this.selection();
      if (s.field != s.base) {
        this.delete_selection(s);
        s = this.selection();
      }

      // Insert statements from clipboard.
      let c = clip.firstChild;
      while (c) {
        let prop = c.firstChild;
        let val = c.lastChild;
        if ((c instanceof FactStatement) &&
            (prop instanceof FactProperty) &&
            (val instanceof FactValue)) {
          let stmt = new FactStatement({
            property: prop.value(),
            value: val.value(),
            qualified: c.className == "qualified",
          });
          this.insertBefore(stmt, s.statement);
        }
        c = c.nextSibling;
      }
      s.selection.collapse(s.statement);
    } else {
      // Paste as text.
      let text = clip.innerText;
      document.execCommand("insertText", false, text);
    }
    this.dirty = true;
  }

  searchbox(field, results) {
    let field_bbox = field.getBoundingClientRect();
    let list_bbox = this.list.parentNode.getBoundingClientRect();
    this.list.style.top = (field_bbox.bottom - list_bbox.top + 6) + "px";
    this.list.style.left = (field_bbox.left - list_bbox.left) + "px";
    this.list.update({items: results});
  }

  searching() {
    let items = this.list.state && this.list.state.items;
    return items && items.length > 0;
  }

  delete_selection(s) {
    let anchor = s.base && s.base.closest("fact-statement");
    let focus = s.statement;
    if (!anchor || !focus);

    let [start, end] = range(anchor, focus);
    let c = start;
    while (c && c != end) {
      let next = c.nextSibling;
      c.remove();
      c = next;
    }
    this.dirty = true;
  }

  selection() {
    let s = document.getSelection();
    if (!s.focusNode) return;

    var statement;
    let field = s.focusNode;
    if (!field) return;
    if (field instanceof FactEditor) {
      statement = field.children.item(s.focusOffset);
      if (!statement) return;
      field = statement.firstChild;
    } else if (field instanceof FactStatement) {
      statement = field;
      field = statement.firstChild;
    } else if (field.nodeType == Node.TEXT_NODE) {
      field = field.parentElement;
    }

    let base = s.anchorNode;
    if (base) {
      if (base instanceof FactEditor) {
        base = base.children.item(s.anchorOffset).firstChild;
      } else if (base instanceof FactStatement) {
        base = base.firstChild;
      } else if (base.nodeType == Node.TEXT_NODE) {
        base = base.parentElement;
      }
    }

    statement = statement || field.parentElement;
    if (!(statement instanceof FactStatement)) return;

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

    // Add ids.
    for (let id of topic.all(n_id)) {
      s.push(n_id);
      s.push(id);
    }

    // Add facts.
    for(let e = this.firstChild; e; e = e.nextSibling) {
      if (!(e instanceof FactStatement)) continue;
      if (e.empty()) continue;

      let prop = e.firstChild;
      let value = e.lastChild;
      if (!prop || !value) continue;
      let p = prop.value();
      let v = value.value();
      if (!p || !v) continue;
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

    // Add media.
    for (let id of topic.all(n_media)) {
      s.push(n_media);
      s.push(id);
    }

    return s;
  }

  render() {
    let topic = this.state;
    if (!topic) return;

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
  get qualified() {
    return this.state && this.state.qualified;
  }

  set qualified(v) {
    if (!this.state) this.state = {};
    this.state.qualified = v;
    this.className = v ? "qualified" : "";
  }

  placeholder() {
    return this.childElementCount != 2;
  }

  empty() {
    if (this.placeholder()) return true;
    return this.firstChild.empty() && this.lastChild.empty();
  }

  render() {
    let statement = this.state;
    if (!statement) return "";
    this.qualified = statement.qualified;
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

class FactField extends Component {
  oninit() {
    this.list = this.match("fact-panel").list();
  }

  async onchanged(e) {
    // Trim content.
    if (this.childElementCount != 0) {
      this.innerHTML = Component.escape(this.text());
    }

    // Check if content has changed.
    let text = this.text();
    if (text != this.getAttribute("text")) {
      // Clear annotation.
      this.removeAttribute("value");
      this.removeAttribute("text");
      this.className = "";

      // Search for matches.
      let results = await search(this.text(), this.backends());
      this.match("fact-editor").searchbox(this, results);
    }
  }

  onenter(e) {
    e.preventDefault();
    let item = this.list.active && this.list.active.state;
    if (item) {
      this.select(item);
    } else {
      this.collapse();
    }
  }

  ontab(e) {
    e.preventDefault();
    let item = this.list.active && this.list.active.state;
    if (!item && this.list.state) {
      let items = this.list.state.items;
      if (items && items.length > 0) item = items[0].state;
    }
    if (item) {
      this.select(item);
    }
    let next = this.nextSibling;
    if (next) window.getSelection().setBaseAndExtent(next, 0, next, 0);
  }

  onselect(e) {
    let item = e.detail.item;
    let keep = e.detail.keep;
    if (!keep) this.list.expand(false);
    if (item && item.state) {
      this.select(item.state);
    }
  }

  next() {
    this.list.next();
  }

  prev() {
    this.list.prev();
  }

  text() {
    return this.innerText.trim().replace(/\s/g, " ");
  }

  empty() {
    return this.text().length == 0;
  }

  textlen() {
    return this.firstChild && this.firstChild.length;
  }

  value() {
    if (this.hasAttribute("value")) {
      let value = this.getAttribute("value");
      return store.parse(value);
    } else {
      let text = this.text();
      if (this.state && this.state.property.get(n_target) == n_quantity_type) {
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

    // Set field value.
    this.collapse();
    this.setAttribute("value", value);
    this.setAttribute("text", this.text());
    this.className = encoded ? "encoded" : "";
    this.innerHTML = Component.escape(text);

    let range = document.createRange();
    if (this.nextSibling) {
      // Move to next field.
      let next = this.nextSibling;
      range.selectNodeContents(next);
      range.collapse(true);

      // Set property for value.
      if (item.ref) {
        next.state.property = store.lookup(item.ref);
      }
    } else {
      // Move cursor to end of field.
      range.selectNodeContents(this);
      range.collapse(false);
    }
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
      let value = val && val.id;
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

    return Component.escape(text);
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
        min-height: 1.25em;
        margin-right: 4px;
        white-space: nowrap;
      }
      $::after {
        content: ":";
      }
      $.encoded {
        color: #0000dd; /*#0645AD;*/ /*#0b0080;*/
      }
      .qualified $ {
        font-weight: normal;
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
        min-height: 1.25em;
      }
      $.encoded {
        color: #0000dd; /*#0645AD;*/ /*#0b0080;*/
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


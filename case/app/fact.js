// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {MdSearchList} from "/common/lib/material.js";
import {Frame, QString, Printer} from "/common/lib/frame.js";
import {store, settings} from "./global.js";
import {value_text, value_parser} from "./value.js";
import {Context} from "./plugins.js";
import {search} from "./omnibox.js";
import {qualified, psearch} from "./schema.js";

const n_id = store.id;
const n_is = store.is;
const n_name = store.lookup("name");
const n_media = store.lookup("media");
const n_target = store.lookup("target");
const n_item_type = store.lookup("/w/item");
const n_quantity_type = store.lookup("/w/quantity");
const n_start_time = store.lookup("P580");
const n_end_time = store.lookup("P582");
const n_point_in_time = store.lookup("P585");

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

function previous_statement(stmt) {
  if (!stmt) return;
  let prev = stmt.previousSibling;
  if (!prev) return;
  if (stmt.qualified) {
    if (!prev.qualified) return;
  } else {
    while (prev && prev.qualified) prev = prev.previousSibling;
  }
  return prev;
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
    }
    if (!this.lastChild.empty()) {
      let placeholder = new FactStatement();
      this.appendChild(placeholder);
    }
  }

  onenter(e) {
    e.preventDefault();
    let s = this.selection();
    if (document.getSelection().isCollapsed) {
      if (s.field && s.field.isnote()) {
        // Split fact note.
        let note = s.field.text();
        let pos = s.position;
        let first = note.slice(0, pos);
        let second = note.slice(pos);
        s.field.update({
          property: null,
          value: first,
        });
        let newstmt = new FactStatement({
          property: null,
          value: second,
          qualified: s.statement.qualified,
        });
        this.insertBefore(newstmt, s.statement.nextSibling);
        document.getSelection().collapse(newstmt.lastChild, 0);
        this.dirty = true;
      } else {
        // Insert new placeholder.
        let placeholder = new FactStatement();
        let point = s && s.statement;
        if (point && point.qualified) placeholder.qualified = true;
        if (point && s.field == s.value) point = point.nextSibling;
        this.insertBefore(placeholder, point);
        document.getSelection().collapse(placeholder, 0);
      }
    } else if (s.field && s.base && s.field != s.base) {
      // Replace selection with placeholder.
      let pos = this.delete_selection(s);
      let placeholder = new FactStatement();
      this.insertBefore(placeholder, pos);
      s.selection.collapse(placeholder);
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

  onleft(e) {
    if (e.ctrlKey && e.shiftKey) {
      let s = document.getSelection();
      if (s.focusNode && s.anchorNode) {
        if (s.focusNode instanceof FactStatement) {
          if (s.anchorNode.nextSibling == s.focusNode) {
            let field = s.anchorNode.firstChild;
            s.selectAllChildren(field);
            e.preventDefault();
          }
        }
      }
    }
  }

  onright(e) {
    if (e.ctrlKey && e.shiftKey) {
      let s = document.getSelection();
      if (s.focusNode && s.anchorNode) {
        if (s.focusNode instanceof FactStatement) {
          if (s.focusNode.nextSibling == s.anchorNode) {
            let field = s.focusNode.lastChild;
            s.selectAllChildren(field);
            e.preventDefault();
          }
        }
      }
    }
  }

  ontab(e) {
    e.preventDefault();
    let forward = !e.shiftKey;
    let s = this.selection();
    if (!s) return;

    if (!s.field && s.statement) {
      // Repeat previous property.
      let prevstmt = previous_statement(s.statement);
      let prevprop = prevstmt && prevstmt.firstChild;
      if (forward && prevprop) {
        // Repeat previous property.
        let prop = new FactProperty({property: "", value: prevprop.value()});
        let value = new FactValue({property: "", value: ""});
        s.statement.appendChild(prop);
        s.statement.appendChild(value);
        s.statement.qualified = prevstmt.qualified;
        s.selection.setBaseAndExtent(value, 0, value, 0);
      }
    } else {
      // Move to next/previous value.
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
      if (s.field && s.field == s.value && s.field.isnote()) {
        let prevstmt = s.statement.previousSibling;
        if (prevstmt && prevstmt.lastChild && prevstmt.lastChild.isnote()) {
          // Merge note with previous note.
          let prevnote = prevstmt.lastChild;
          let first = prevnote.text();
          let second = s.field.text();
          prevnote.update({
            property: null,
            value: first + second,
          });
          s.statement.remove();
          document.getSelection().collapse(prevnote.firstChild, first.length);
          this.dirty = true;
        }
      } else {
        if (s.statement.qualified) {
          // Unqualify statement.
          s.statement.qualified = false;
          this.dirty = true;
        } else {
          // Delete empty statement.
          let prev = s.statement.previousSibling;
          if (prev && prev.empty()) {
            prev.remove();
          } else if (s.statement.empty()) {
            s.statement.remove();
          }
        }
      }
    }

    if (s.statement && s.field != s.base) {
      // Delete selection.
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
      this.dirty = true;
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
    } else if (e.key === "ArrowLeft") {
      this.onleft(e);
    } else if (e.key === "ArrowRight") {
      this.onright(e);
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
          // Add stub for pasted items.
          if (val.className == "encoded") {
            let v = store.parse(val.getAttribute("value"))
            if ((v instanceof Frame) && v.isproxy()) {
              v.add(n_name, val.getAttribute("text"));
              v.markstub();
            }
          }

          // Add new statement.
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
    e.stopPropagation();
    e.preventDefault();
  }

  searchbox(field, results) {
    if (this.focused == field) {
      this.list.update({items: results});
    } else {
      let field_bbox = field.getBoundingClientRect();
      let list_bbox = this.list.parentNode.getBoundingClientRect();
      this.list.style.top = (field_bbox.bottom - list_bbox.top + 6) + "px";
      this.list.style.left = (field_bbox.left - list_bbox.left) + "px";
      this.list.update({items: results});
      this.focused = field;
    }
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
    s.selection.collapse(end);
    return end;
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
      if (p === undefined || v === undefined) continue;
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
  constructor(state) {
    super(state);
    if (state && state.qualified) this.className = "qualified";
  }

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
    if (statement.property === undefined && statement.value === undefined) {
      return "";
    } else {
      return [
        new FactProperty({property: n_item_type, value: statement.property}),
        new FactValue({property: statement.property, value: statement.value}),
      ];
    }
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

function parse_date(date) {
  let results = new Array();
  value_parser(date, results);
  if (results.length > 0 && results[0].description == "time") {
    return results[0].value;
  }
}

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

      if (text == "*") {
        // Start note.
        this.collapse();
        this.setAttribute("value", "nil");
        this.setAttribute("text", "⚫︎");
        this.className = "note";
        this.innerHTML = "⚫︎";

        // Move focus to note.
        let selection = window.getSelection();
        selection.selectAllChildren(this.nextSibling);
        selection.collapseToStart();
      } else if (!this.isnote()) {
        // Split out temporal modifiers in brackets.
        let modifiers;
        let query = this.text();
        let m = query.match(/^(.*)\[(.*)\]\s*$/);
        if (m) {
          // Split time interval.
          modifiers = {};
          query = m[1];
          let delim = m[2].indexOf("--");
          if (delim == -1) delim = m[2].indexOf(" - ");
          if (delim == -1) delim = m[2].indexOf(":");
          if (delim == -1) {
            modifiers.time = parse_date(m[2].trim());
          } else {
            let start = m[2].slice(0, delim).trim();
            let end = m[2].slice(delim + 2).trim();
            if (start) modifiers.start = parse_date(start);
            if (end) modifiers.end = parse_date(end);
          }
        }

        // Search for matches.
        let results = await search(query, this.backends());
        if (modifiers) {
          for (let r of results) r.state.modifiers = modifiers;
        }
        this.match("fact-editor").searchbox(this, results);
      }
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
      let property = this.state && this.state.property;
      if (property &&
          (property instanceof Frame) &&
          property.get(n_target) == n_quantity_type) {
        let number = parseFloat(text);
        if (!isNaN(number)) return number;
      }
      return text;
    }
  }

  isnote() {
    return this.previousSibling && this.previousSibling.className == "note";
  }

  add_qualifier(property, value) {
    let qualifier = new FactStatement({property, value, qualified: true});
    let stmt = this.parentElement;
    let next = stmt.nextSibling;
    while (next && next.qualified) next = next.nextSibling;
    stmt.parentElement.insertBefore(qualifier, next);
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
        if (value instanceof Frame) {
          text = value.get(n_name) || value.id;
        }
      }
    } else if (item.topic) {
      if (item.casefile) {
        // Create new topic with reference to topic in external case.
        let editor = this.match("#editor");
        let link = editor.new_topic();
        link.add(n_is, item.topic);
        let name = item.topic.get(n_name);
        if (name) link.add(n_name, name);
        await editor.update_topics();
        value = link.id;
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

    // Add modifiers.
    if (item.modifiers) {
      if (item.modifiers.start) {
        this.add_qualifier(n_start_time, item.modifiers.start);
      }
      if (item.modifiers.end) {
        this.add_qualifier(n_end_time, item.modifiers.end);
      }
      if (item.modifiers.time) {
        this.add_qualifier(n_point_in_time, item.modifiers.time);
      }
    }
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
        } else if (val instanceof QString) {
          value = val.stringify(store);
        } else {
          value = val;
        }
      }
      this.setAttribute("value", value);
      this.setAttribute("text", text);
      this.className = "encoded";
    } else if (val == null) {
      this.setAttribute("value", "nil");
      this.setAttribute("text", "⚫︎");
      this.className = "note";
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
      $.note::after {
        content: "";
      }
    `;
  }
}

Component.register(FactProperty);

function newtopic(query, editor, results) {
  results.push({
    ref: query,
    name: query,
    description: "new topic",
    context: new Context(null, editor.casefile, editor),
    onitem: item => {
      // Create new topic stub.
      let topic = item.context.new_topic();
      if (!topic) return;
      topic.put(n_name, item.name.trim());
      item.context.select = false;
      return true;
    },
  });
}

class FactValue extends FactField {
  backends() {
    let editor = this.match("#editor");
    return [
      (query, full, results) => value_parser(query, results),
      (query, full, results) => editor.search(query, full, results),
      (query, full, results) => newtopic(query, editor, results),
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


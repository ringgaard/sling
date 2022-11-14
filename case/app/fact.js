// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {MdSearchList} from "/common/lib/material.js";
import {Frame, QString, Printer} from "/common/lib/frame.js";
import {store, frame, settings} from "./global.js";
import {value_text, value_parser, LabelCollector} from "./value.js";
import {Context} from "./plugins.js";
import {search, kbsearch} from "./search.js";
import {qualified, psearch} from "./schema.js";

const n_id = store.id;
const n_is = store.is;
const n_name = frame("name");
const n_media = frame("media");
const n_internal = frame("internal");
const n_target = frame("target");
const n_item_type = frame("/w/item");
const n_quantity_type = frame("/w/quantity");
const n_xref_type = frame("/w/xref");
const n_start_time = frame("P580");
const n_end_time = frame("P582");
const n_point_in_time = frame("P585");
const n_instance_of = frame("P31");
const n_human = frame("Q5");
const n_gender = frame("P21");
const n_female = frame("Q6581072");
const n_male = frame("Q6581097");

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
        display: block;
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

    this.attach(this.onkeydown, "keydown");
    this.attach(this.onclick, "click");
    this.attach(this.oncut, "cut");
    this.attach(this.oncopy, "copy");
    this.attach(this.onpaste, "paste");

    this.attach(this.oninput, "input");
    this.attach(this.onbeforeinput, "beforeinput");
    this.attach(this.onfocusin, "focusin");
    this.attach(this.onfocusout, "focusout");
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
    // Keep track of focused topic.
    if (this.focused && this.focused != focus.field) {
      this.focused.collapse();
      this.focused = null;
    }

    // Scroll current statement into view.
    let cursor = focus.field || focus.statement;
    if (cursor) {
      if (cursor.scrollIntoViewIfNeeded) {
        cursor.scrollIntoViewIfNeeded(false);
      } else {
        cursor.scrollIntoView({block: "nearest"});
      }
    }
  }

  onbeforeinput(e) {
    let selection = document.getSelection();
    let focus = selection.focusNode;
    if (!focus) return;
    if ((focus instanceof FactStatement) && focus.placeholder()){
      // Add property and value to current placeholder.
      let prop = new FactProperty({property: "", value: ""});
      let val = new FactValue({property: "", value: ""});
      focus.appendChild(prop);
      focus.appendChild(val);

      if (e.data == " ") {
        // Make statement qualified on space.
        focus.qualified = true;
      } else {
        // Add initial character to property.
        let initial = document.createTextNode(e.data);
        prop.appendChild(initial);
        selection.collapse(initial, e.data.length);
        prop.onchanged();
      }
      e.preventDefault();
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
      s.field.onchanged();
    }
    if (!this.lastChild.empty()) {
      let placeholder = new FactStatement();
      this.appendChild(placeholder);
    }
  }

  onenter(e) {
    e.preventDefault();
    let s = this.selection();
    if (!s) return;
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
        e.preventDefault();
      }
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
            if (s.statement == this.lastChild) {
              s.statement.clear();
            } else {
              s.statement.remove();
            }
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
      if (s.statement != this.lastChild) {
        s.statement.remove();
        this.dirty = true;
      }
    } else if (s.statement && s.field != s.base) {
      e.preventDefault();
      this.delete_selection(s);
    } else if (s.field && s.position == s.field.textlen()) {
      e.preventDefault();
    }
  }

  onmenu(e) {
    let s = this.selection();
    if (s && s.field) {
      if (this.searching()) {
        s.field.collapse();
      } else {
        s.field.expand();
      }
    }
  }

  onkeydown(e) {
    if (e.code === "ArrowUp") {
      if (this.searching()) {
        this.focused.prev();
        e.preventDefault();
      } else {
        this.onmove(e, false);
      }
    } else if (e.code === "ArrowDown") {
      if (this.searching()) {
        this.focused.next();
        e.preventDefault();
      } else {
        this.onmove(e, true);
      }
    } else if (e.code === "ArrowLeft") {
      this.onleft(e);
    } else if (e.code === "ArrowRight") {
      this.onright(e);
    } else if (e.code === "Tab") {
      if (this.searching()) {
        this.focused.ontab(e);
      } else {
        this.ontab(e);
      }
    } else if (e.code === "Home") {
      this.onhome(e);
    } else if (e.code === "End") {
      this.onend(e);
    } else if (e.code === "Backspace") {
      this.onbackspace(e);
    } else if (e.code === "Delete") {
      this.ondelete(e);
    } else if (e.code === "Enter") {
      if (this.searching()) {
        this.focused.onenter(e);
      } else {
        this.onenter(e);
      }
    } else if (e.code === "Escape") {
      if (this.searching()) {
        this.focused.collapse();
        e.stopPropagation();
      }
    } else if (e.code === "KeyF" && e.ctrlKey) {
      this.human(n_female);
      e.stopPropagation();
      e.preventDefault();
    } else if (e.code == "KeyM" && e.ctrlKey) {
      this.human(n_male);
      e.stopPropagation();
      e.preventDefault();
    } else if (e.code == "Space" && e.shiftKey) {
      e.preventDefault();
      this.onmenu(e);
    } else if (e.code == "Space") {
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
    if (s && s.field != s.base) {
      e.preventDefault();
      document.execCommand("copy");
      this.delete_selection(s);
    }
  }

  oncopy(e) {
    let s = this.selection();
    if (s && s.field == s.base) {
      if (s.selection.isCollapsed) {
        let range = document.createRange();
        if (s.field.className == "encoded") {
          range.selectNode(s.field);
        } else {
          range.selectNodeContents(s.field);
        }
        s.selection.removeAllRanges();
        s.selection.addRange(range);
      }
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

    if (clip.querySelector("fact-statement,fact-property")) {
      // Clear select before paste.
      let s = this.selection();
      if (s.field != s.base) {
        this.delete_selection(s);
        s = this.selection();
      }

      // Insert statements from clipboard.
      for (let c = clip.firstChild; c; c = c.nextSibling) {
        // Get property and/or value.
        let prop = null;
        let val = null;
        if (c instanceof FactStatement) {
          if (c.firstChild instanceof FactProperty) prop = c.firstChild;
          if (c.lastChild instanceof FactValue) val = c.lastChild;
        } else if (c instanceof FactProperty) {
          prop = c;
        } else if (c instanceof FactValue) {
          val = c;
        } else {
          continue;
        }

        // Add stub for pasted items.
        let encoded = false;
        if (val && val.className == "encoded") {
          let v = store.parse(val.getAttribute("value"))
          if ((v instanceof Frame) && v.isproxy()) {
            v.add(n_name, val.getAttribute("text"));
            v.markstub();
          }
          encoded = true;
        }

        if (prop && val) {
          // Add new statement.
          let stmt = new FactStatement({
            property: prop.value(),
            value: val.value(),
            qualified: c.className == "qualified",
          });
          this.insertBefore(stmt, s.statement);
          s.selection.collapse(s.statement);
        } else if (prop || val) {
          val = val || prop;
          let selection = document.getSelection();
          let focus = selection.focusNode;
          if ((focus instanceof FactStatement) && focus.placeholder()) {
            // Replace placeholder with new statement with value.
            let p = new FactProperty({property: "", value: val.value()});
            let v = new FactValue({property: "", value: ""});
            focus.appendChild(p);
            focus.appendChild(v);
            selection.collapse(v);
          } else if (focus instanceof FactField) {
            // Insert pasted value into field.
            focus.update_value(val.text(), val.value(), encoded);
            selection.collapse(focus, 1);
          }
        }
      }
    } else {
      // Add placeholder if needed before inserting text.
      let selection = document.getSelection();
      let focus = selection.focusNode;
      if ((focus instanceof FactStatement) && focus.placeholder()){
        let p = new FactProperty({property: "", value: ""});
        let v = new FactValue({property: "", value: ""});
        focus.appendChild(p);
        focus.appendChild(v);
        selection.collapse(p, 0);
      }

      // Paste as text.
      let text = clip.innerText.trim().replace(/\n/g, " ");
      document.execCommand("insertText", false, text);
    }
    this.dirty = true;
    e.stopPropagation();
    e.preventDefault();
  }

  async human(gender) {
    let s = this.selection();
    if (!s.statement) return;

    let labels = new LabelCollector(store);
    labels.add_item(n_human);
    labels.add_item(gender);
    await labels.retrieve();

    if (this.value(n_instance_of.id) != n_human) {
      let t = new FactStatement({property: n_instance_of, value: n_human});
      this.insertBefore(t, s.statement);
      this.dirty = true;
    }


    if (this.value(n_gender.id) != gender) {
      let g = new FactStatement({property: n_gender, value: gender});
      this.insertBefore(g, s.statement);
      this.dirty = true;
    }
  }

  searchbox(field, results) {
    if (field != this.focused) {
      // Place search list below field.
      let field_bbox = field.getBoundingClientRect();
      let panel_bbox = this.list.parentNode.getBoundingClientRect();

      let top = Math.round(field_bbox.bottom - panel_bbox.top);
      let left = Math.round(field_bbox.left - panel_bbox.left);

      this.list.style.top = top + "px";
      this.list.style.left = left + "px";
      this.focused = field;
    }

    // Update search results.
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
    s.selection.collapse(end);
    return end;
  }

  value(prop) {
    for (let stmt = this.firstChild; stmt; stmt = stmt.nextSibling) {
      if (stmt.placeholder()) continue;
      if (stmt.qualified) continue;
      if (stmt.property().getAttribute("value") == prop) {
        return stmt.value().value();
      }
    }
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
        base = base.firstChild || base;
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
    for (let e = this.firstChild; e; e = e.nextSibling) {
      if (!(e instanceof FactStatement)) continue;
      if (e.empty()) continue;

      let prop = e.property();
      let value = e.value();
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

    // Add internals.
    let internal = topic.get(n_internal);
    if (internal) {
      s.push(n_internal);
      s.push(internal);
    }

    return s;
  }

  render() {
    let topic = this.state;
    if (!topic) return;

    let h = new Array();
    for (let i = 0; i < topic.length; ++i) {
      let prop = topic.name(i);
      if (prop == n_id || prop == n_media || prop == n_internal) continue;
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

  clear() {
    if (this.firstChild) this.firstChild.remove();
    if (this.lastChild) this.lastChild.remove();
  }

  property() {
    return this.firstChild;
  }

  value() {
    return this.lastChild;
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
  if (results.length > 0 && results[0].isdate) {
    return results[0].value;
  }
}

class FactField extends Component {
  oninit() {
    this.list = this.match("fact-panel").list();
  }

  async onchanged() {
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
          let mod = m[2].trim();
          let dstart = undefined;
          let dend = undefined;
          for (let delim of ["--", " - ", ":"]) {
            let p = mod.indexOf(delim);
            if (p != -1) {
              dstart = p;
              dend = p + delim.length;
              break;
            }
          }
          if (dstart && dend) {
            let start = mod.slice(0, dstart).trim();
            let end = mod.slice(dend).trim();
            if (start) modifiers.start = parse_date(start);
            if (end) modifiers.end = parse_date(end);
          } else {
            modifiers.time = parse_date(mod);
          }
        }

        // Search for matches.
        let context = this.search();
        let results = await search(query, context.backends, context.options);
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
    if (item && !item.newtopic) {
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
        let link = await editor.new_topic();
        link.add(n_is, item.topic.id);
        let name = item.topic.get(n_name);
        if (name) link.add(n_name, name);
        await editor.update_topics();
        value = link.id;
        text = name;
      } else if (this.state.property == n_is) {
        value = null;
        text = item.topic.id;
        encoded = false;
      } else {
        value = item.topic.id;
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
      if (this.state.property == n_is) {
        value = null;
        text = item.ref;
        encoded = false;
      } else {
        value = item.ref;
        text = item.name;
      }
    }

    // Set field value.
    this.collapse();
    this.update_value(text, value, encoded);

    // Move to next field.
    let range = document.createRange();
    if (this.nextSibling) {
      // Move to next field.
      let next = this.nextSibling;
      range.selectNodeContents(next);
      range.collapse(true);

      // Set property for value.
      if (item.ref) {
        next.state.property = frame(item.ref);
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

  expand() {
    this.setAttribute("text", "");
    this.onchanged();
  }

  update_value(text, value, encoded) {
    if (value) {
      this.setAttribute("value", value);
    } else {
      this.removeAttribute("value");
    }
    this.setAttribute("text", text);
    this.className = encoded ? "encoded" : "";
    this.innerHTML = Component.escape(text);
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
  search() {
    let options = {};
    let backends = [psearch];

    let stmt = this.parentElement;
    if (stmt.qualified) {
      while (stmt && stmt.qualified) stmt = stmt.previousSibling;
      if (stmt && !stmt.empty()) {
        options.qualify = stmt.property().value();
      }
    } else {
      let editor = this.closest("fact-editor");
      options.type = editor.value("P31");
      options.occupation = editor.value("P106");
    }

    return {options, backends};
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

function newtopic(query, editor, field, results) {
  results.push({
    ref: query,
    name: query,
    description: "new topic",
    newtopic: true,
    context: new Context(null, editor.casefile, editor),
    onitem: async item => {
      // Create new topic stub.
      let position = field.match("topic-card").state;
      let topic = await item.context.new_topic(position);
      if (!topic) return;
      topic.put(n_name, item.name.trim());

      // Move xref qualifiers to new topic.
      let e = field.parentElement.nextSibling;
      while (e) {
        if (!e.qualified) break;
        let prop = e.property().value();
        let value = e.value().value();
        if (prop.get && prop.get(n_target) == n_xref_type) {
          topic.put(prop, value);
          let next = e.nextSibling;
          e.remove();
          e = next;
        } else {
          e = e.nextSibling;
        }
      }

      item.context.select = false;
      return true;
    },
  });
}

class FactValue extends FactField {
  search() {
    let editor = this.match("#editor");
    return {
      options: {
        property: this.previousSibling.value(),
      },
      backends: [
        (query, results, options) => value_parser(query, results),
        editor.search.bind(editor),
        kbsearch,
        (query, results, options) => newtopic(query, editor, this, results),
      ],
    };
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


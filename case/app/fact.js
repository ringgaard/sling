// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {Frame} from "/common/lib/frame.js";
import {store, settings} from "./global.js";
import {value_text} from "./value.js";

const n_id = store.id;
const n_is = store.is;
const n_name = store.lookup("name");
const n_media = store.lookup("media");
const n_item_type = store.lookup("/w/item");

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
  }

  onmove(e, down) {
    let s = this.selection();
    if (!s || s.position != 0) return;
    let next = down ? s.statement.nextSibling : s.statement.previousSibling;
    if (!next) return;

    let focus = next.firstChild;
    if (!focus) return;
    if (s.field == s.value) focus = focus.nextSibling;
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

    console.log("s", s);
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
    console.log("s", s);
    if (s && s.field == s.property && s.position == 0) {
      s.statement.qualified = false;
      e.preventDefault();
    }
  }

  ondelete(e) {
    let s = this.selection();
    console.log("del", s);
  }

  onkeydown(e) {
    if (e.key === "ArrowUp") {
      this.onmove(e, false);
    } else if (e.key === "ArrowDown") {
      this.onmove(e, true);
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

    let base = s.anchorNode;
    if (base && base.nodeType == Node.TEXT_NODE) {
      base = field.parentElement;
    }

    let statement = field.parentElement;
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
        min-height: 1em;
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

    return Component.escape(text);
  }
}

Component.register(FactField);

class FactProperty extends FactField {
  static stylesheet() {
    return `
      $ {
        display: flex;
        font-weight: 500;
        margin-right: 4px;
        white-space: nowrap;
      }
      $::after {
        content: ":";
      }
      $.encoded {
        color: #0b0080;
      }
      .qualified $ {
        font-weight: normal;
      }
    `;
  }
}

Component.register(FactProperty);

class FactValue extends FactField {
  static stylesheet() {
    return `
      $ {
        display: flex;
      }
      $.encoded {
        color: #0b0080;
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
  if (selection.isCollapsed) return;

  let focus = closest_fact(selection.focusNode);
  if (!focus) return;
  let anchor = closest_fact(selection.anchorNode);
  if (!anchor) return;

  // Ignore selection within fact field.
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


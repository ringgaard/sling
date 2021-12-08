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
    this.bind(null, "paste", e => this.onpaste(e));
  }

  onmove(e, down) {
    let selection = document.getSelection();
    if (!selection.focusNode || selection.focusOffset != 0) return;
    let field = selection.focusNode;
    if (!field) return;
    if (field.nodeType == Node.TEXT_NODE) field = field.parentElement;
    let stmt = field.parentElement;
    if (!stmt) return;
    let next = down ? stmt.nextSibling : stmt.previousSibling;
    if (!next) return;

    let focus = next.firstChild;
    if (field instanceof FactValue) focus = focus.nextSibling;
    if (!focus) return;

    let anchor = e.shiftKey ? selection.anchorNode : focus;
    let anchorofs = e.shiftKey ? selection.anchorOffset : 0;
    selection.setBaseAndExtent(anchor, anchorofs, focus, 0);
    e.preventDefault();
  }

  ontab(e) {
    e.preventDefault();
    let forward = !e.shiftKey;
    let selection = document.getSelection();
    let field = selection.focusNode;
    if (!field) return;
    if (field.nodeType == Node.TEXT_NODE) field = field.parentElement;

    var focus = forward ? field.nextSibling : field.previousSibling;
    if (!focus) {
      let stmt = field.parentElement;
      if (!stmt) return;
      let next = forward ? stmt.nextSibling : stmt.previousSibling;
      if (!next) return;
      focus = forward ? next.firstChild : next.lastChild;
    }
    if (!focus) return;

    selection.setBaseAndExtent(focus, 0, focus, 0);
  }

  onhome(e) {
    e.preventDefault();
    let selection = document.getSelection();
    let field = selection.focusNode;
    if (!field) return;
    if (field.nodeType == Node.TEXT_NODE) field = field.parentElement;
    let home = field.previousSibling || field;
    if (!home) return;

    let anchor = e.shiftKey ? selection.anchorNode : home;
    let anchorofs = e.shiftKey ? selection.anchorOffset : 0;
    selection.setBaseAndExtent(anchor, anchorofs, home, 0);
  }

  onend(e) {
    e.preventDefault();
    let selection = document.getSelection();
    let field = selection.focusNode;
    if (!field) return;
    if (field.nodeType == Node.TEXT_NODE) field = field.parentElement;
    let end = field.nextSibling || field;
    if (!end) return;

    let anchor = e.shiftKey ? selection.anchorNode : end;
    let anchorofs = e.shiftKey ? selection.anchorOffset : 1;
    selection.setBaseAndExtent(anchor, anchorofs, end, 1);
  }


  onkeydown(e) {
    if (e.key == "ArrowUp") {
      this.onmove(e, false);
    } else if (e.key == "ArrowDown") {
      this.onmove(e, true);
    } else if (e.key == "Tab") {
      this.ontab(e);
    } else if (e.key == "Home") {
      this.onhome(e);
    } else if (e.key == "End") {
      this.onend(e);
    }
  }

  onupdated() {
    this.focus();
  }

  onpaste(e) {
    console.log("paste", e);
    let html = e.clipboardData.getData('text/html');
    let doc = new DOMParser().parseFromString(html, "text/html");
    console.log("doc", doc);
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
  render() {
    let statement = this.state;
    this.className = statement.qualified ? "qualified" : "";
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
    let val = this.state.value;
    let prop = this.state.type;
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
      }
      $.encoded {
        color: #0b0080;
      }
    `;
  }
}

Component.register(FactValue);


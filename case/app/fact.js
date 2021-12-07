// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {Frame} from "/common/lib/frame.js";
import {store, settings} from "./global.js";

const n_id = store.id;
const n_is = store.is;
const n_name = store.lookup("name");
const n_media = store.lookup("media");

function qualified(v) {
  return v instanceof Frame ? v.has(n_is) : false;
}

class FactEditor extends Component {
  visible() {
    return this.state;
  }

  oninit() {
    this.setAttribute("contenteditable", true);
    this.setAttribute("spellcheck", false);
    this.focus();
  }

  onupdated() {
    this.focus();
  }

  render() {
    let topic = this.state;
    if (!topic) return;
    let h = new Array();
    for (let i = 0; i < topic.length; ++i) {
      let property = topic.name(i);
      if (property == n_id || property == n_media) continue;
      let value = topic.value(i);
      if (qualified(value)) {
        h.push(new FactStatement({property, value: value.get(n_is)}));
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
        h.push(new FactStatement({property, value}));
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
        border: thin solid lightgrey
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
      new FactProperty(statement.property),
      new FactValue(statement.value),
    ];
  }

  static stylesheet() {
    return `
      $ {
        display: block;
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

class FactProperty extends Component {
  render() {
    let property = this.state;
    if (property instanceof Frame) {
      let text = property.get(n_name);
      if (!text) text = property.id;
      this.className = "link";
      return text;
    } else {
      return property;
    }
  }

  static stylesheet() {
    return `
      $ {
        font-weight: 500;
        padding-right: 4px;
      }
      $::after {
        content: ":";
      }
      $.link {
        color: #0b0080;
      }
      .qualified $ {
        font-weight: normal;
      }
    `;
  }
}

Component.register(FactProperty);

class FactValue extends Component {
  render() {
    let value = this.state;
    if (value instanceof Frame) {
      let text = value.get(n_name);
      if (!text) text = value.id;
      this.className = "link";
      return text;
    } else {
      this.className = "";
      return value;
    }
  }

  static stylesheet() {
    return `
      $ {
      }
      $.link {
        color: #0b0080;
      }
    `;
  }
}

Component.register(FactValue);


// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Web component framework.

// HTML string escape.
const htmlmap = {
  '"': '&quot;',
  '&': '&amp;',
  '\'': '&#x27;',
  '<': '&lt;',
  '>': '&gt;',
  '`': '&#x60;',
  '/': '&#x2F;'
}

// Registered style sheet functions.
var stylesheets = [];

// Defer showing page until loaded.
window.addEventListener('load', (e) => {
  document.body.style.display = "";
});

// Base class for web components.
export class Component extends HTMLElement {
  // Initialize new web component.
  constructor(state) {
    super();
    this.state = state;
    this.props = {};
    this.elements = [...this.children];
    this.nodes = [...this.childNodes];
  }

  // Connect web component to DOM.
  connectedCallback() {
    // Add attributes to properties.
    for (const attr of this.attributes) {
      let name = attr.name.replace(/-/g, "_");
      let value = attr.value;

      if (value == "null") {
        value = null;
      } else if (value == "true") {
        value = true;
      } else if (value == "false") {
        value = false;
      } else {
        let n = parseFloat(value);
        if (!isNaN(n) && isFinite(n)) value = n;
      }

      this.props[name] = value;
    }

    // Render component.
    this.onconnect && this.onconnect();
    if (this.visible) {
      if (this.visible()) {
        this.generate();
      } else {
        this.style.display = "none";
      }
    } else {
      this.generate();
    }
    this.onconnected && this.onconnected();
  }

  // Update component state.
  update(state) {
    this.state = state;

    this.onupdate && this.onupdate();
    if (this.visible) {
      if (this.visible()) {
        this.style.display = "";
        this.generate();
      } else {
        this.style.display = "none";
      }
    } else {
      this.generate();
    }
    this.onupdated && this.onupdated();
  }

  // Generate component content.
  generate() {
    if (this.render) {
      let content = this.render();
      if (content instanceof Array) {
        while (this.firstChild) this.removeChild(this.lastChild);
        for (let n of content) {
          if (n instanceof Node) {
            this.appendChild(n);
          } else {
            this.insertAdjacentHTML("beforeend", n);
          }
        }
      } else if (content instanceof Node) {
        while (this.firstChild) this.removeChild(this.lastChild);
        this.appendChild(content);
      } else if (content != null) {
        this.innerHTML = content;
      }
    }
  }

  // Generate HTML for template.
  html(strings, ...values) {
    let parts = [];
    for (let i = 0; i < strings.length; ++i) {
      parts.push(strings[i]);
      if (i >= values.length) continue;
      let value = values[i];
      parts.push(value);
    }
    return parts.join("");
  }

  // Find matching child element.
  find(selector) {
    if (selector == null || this.matches(selector)) return this;
    return this.querySelector(selector);
  }

  // Find matching parent element.
  match(selector) {
    if (selector == null || this.matches(selector)) return this;
    return this.closest(selector);
  }

  // Bind event handler to selected element.
  bind(selector, event, handler) {
    let elem = this.find(selector);
    if (!elem) {
      throw new Error(`element '${selector}' not found`);
    }
    elem.addEventListener(event, handler);
  }

  static escape(s) {
    if (s == undefined) return "";
    return s.replace(/["&'<>`/]/g, c => htmlmap[c]);
  }

  // Register class as a HTML web component.
  static register(cls, tagname) {
    // Compute tag name from class name if no tag name provided.
    if (!tagname) {
      tagname = cls.name.match(/[A-Z][a-z]*/g).join("-").toLowerCase();
    }

    // Check if tag name has already been registered.
    if (window.customElements.get(tagname)) return;

    // Register custom element.
    window.customElements.define(tagname, cls);

    // Register style sheet for web component.
    if (cls.stylesheet) {
      if (!stylesheets.includes(cls.stylesheet)) {
        stylesheets.push(cls.stylesheet);
        let css = cls.stylesheet().replace(/\$/g, tagname);
        stylesheet(css);
      }
    }
  }
}

// Switch tag for selecting active subcomponent.
export class OneOf extends HTMLElement {
  constructor() {
    super();
    this.elements = [...this.children];
    this.active = this.find(this.getAttribute("selected"));
  }

  connectedCallback() {
    this.innerHTML = "";
    if (this.active) this.appendChild(this.active);
    if (this.onconnected) this.onconnected();
  }

  update(state, substate) {
    let selected = this.find(state);
    if (selected != this.active) {
      if (this.active && this.active.update) this.active.update(null);
      this.innerHTML = "";

      this.active = selected;
      if (this.active) {
        this.appendChild(this.active);
        if (substate && this.active.update) this.active.update(substate);
      }

      if (this.onupdated) this.onupdated();
    }
  }

  find(selector) {
    if (selector == null) return null;
    if (this.matches(selector)) return this;
    for (let elem of this.elements) {
      if (elem.matches(selector)) return elem;
      let child = elem.querySelector(selector);
      if (child) return child;
    }
    return null;
  }

  bind(selector, event, handler) {
    let elem = this.find(selector);
    if (!elem) {
      throw new Error(`element '${selector}' not found`);
    }
    elem.addEventListener(event, handler);
  }
}

window.customElements.define("one-of", OneOf);

// Add global style sheet to document.
export function stylesheet(css) {
  let node = document.createElement("style");
  node.appendChild(document.createTextNode(css));
  if (document.head) document.head.appendChild(node);
}


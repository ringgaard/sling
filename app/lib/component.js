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
    this.initialized = false;
    this.props = {};
    this.elements = [...this.children];
    this.nodes = [...this.childNodes];
  }

  // Initialize component.
  initialize() {
    if (this.initialized) return undefined;
    this.initialized = true;
    return this.oninit && this.oninit();
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
    let p = this.onconnect && this.onconnect();
    if (p instanceof Promise) {
      return p.then(() => {
        if (!this.hide()) {
          this.generate(true);
          let p = this.initialize();
          if (p instanceof Promise) {
            return p.then(() => {
              return this.onconnected && this.onconnected();
            });
          } else {
            return this.onconnected && this.onconnected();
          }
        } else {
          return this.onconnected && this.onconnected();
        }
      });
    } else {
      if (!this.hide()) {
        this.generate(true);
        let p = this.initialize();
        if (p instanceof Promise) {
          return p.then(() => {
            return this.onconnected && this.onconnected();
          });
        } else {
          return this.onconnected && this.onconnected();
        }
      } else {
        return this.onconnected && this.onconnected();
      }
    }
  }

  // Update component state.
  update(state) {
    this.state = state;

    let p = this.onupdate && this.onupdate();
    if (p instanceof Promise) {
      return p.then(() => {
        if (!this.hide()) {
          this.generate(false);
          let p = this.initialize();
          if (p instanceof Promise) {
            return p.then(() => {
              return this.onupdated && this.onupdated();
            });
          } else {
            return this.onupdated && this.onupdated();
          }
        } else {
          return this.onupdated && this.onupdated();
        }
      });
    } else {
      if (!this.hide()) {
        this.generate(false);
        let p = this.initialize();
        if (p instanceof Promise) {
          return p.then(() => {
            return this.onupdated && this.onupdated();
          });
        } else {
          return this.onupdated && this.onupdated();
        }
      } else {
        return this.onupdated && this.onupdated();
      }
    }
  }

  // Hide component if it is not visible.
  hide() {
    if (this.visible) {
      if (this.visible()) {
        this.style.display = "";
        return false;
      } else {
        this.style.display = "none";
        return true;
      }
    }
    return false;
  }

  // Generate component content.
  generate(pre) {
    if (pre && this.prerender) {
      this.mount(this.prerender());
    } else if (this.render) {
      this.mount(this.render());
    }
  }

  // Insert content into element.
  mount(content) {
    if (content instanceof Array) {
      let begin = 0;
      let end = content.length;
      let anchor = null;
      if (end > 0 && this.firstChild) {
        // Find unchanged head.
        let head = this.firstChild;
        while (begin < end && content[begin] == head) {
          head = head.nextSibling;
          begin++;
        }

        // Find unchanged tail.
        let tail = this.lastChild;
        while (begin < end && content[end - 1] == tail) {
          tail = tail.previousSibling;
          end--;
        }

        // Insert new elements before tail.
        if (tail) anchor = tail.nextSibling;

        // Remove all existing elements between head and tail.
        if (head && tail) {
          while (head != tail) {
            let e = tail;
            tail = tail.previousSibling;
            this.removeChild(e);
          }
          this.removeChild(tail);
        }
      } else {
        while (this.firstChild) this.removeChild(this.lastChild);
      }

      for (let i = begin; i < end; ++i) {
        let n = content[i];
        if (n instanceof Node) {
          this.insertBefore(n, anchor);
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

  // Dispatch custom event.
  dispatch(type, detail, bubbles) {
    let event = new CustomEvent(type, {detail, bubbles});
    this.dispatchEvent(event);
  }

  static escape(s) {
    if (s == undefined) return "";
    return s.toString().replace(/["&'<>`/]/g, c => htmlmap[c]);
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
    if (this.onconnect) this.onconnect();
    this.innerHTML = "";
    if (this.active) this.appendChild(this.active);
    if (this.onconnected) this.onconnected();
  }

  update(state, substate) {
    let selected = this.find(state);

    if (this.active) {
      if (this.active.update) this.active.update(null);
      this.innerHTML = "";
    }

    this.active = selected;
    if (this.active) {
      this.appendChild(this.active);
      if (this.active.update) this.active.update(substate);
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


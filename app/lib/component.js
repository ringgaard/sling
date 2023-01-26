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
    this.attrs = {};
  }

  // Initialize component.
  initialize() {
    if (this.initialized) return undefined;
    this.initialized = true;
    return this.oninit && this.oninit();
  }

  // Connect web component to DOM.
  async connectedCallback() {
    // Get element attributes.
    for (const attr of this.attributes) {
      let name = attr.name.replace(/-/g, "_");
      let value = attr.value;

      try {
        let v = JSON.parse(`[${value}]`);
        this.attrs[name] = Array.isArray(v) && v.length == 1 ? v[0] : value;
      } catch (e) {
        this.attrs[name] = value;
      }
    }

    // Render component.
    if (this.onconnect) await this.onconnect();
    if (!this.hide()) {
      if (this.onrender) await this.onrender();
      this.generate();
      await this.initialize();
      if (this.onrendered) await this.onrendered();
    }
    if (this.onconnected) await this.onconnected();
  }

  // Update component state.
  async update(state) {
    this.state = state;
    if (this.onupdate) await this.onupdate();
    if (!this.hide()) {
      this.generate();
      await this.initialize();
      if (this.onrendered) await this.onrendered();
    }
    if (this.onupdated) await this.onupdated();
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
  generate() {
    if (!this.initialized && this.prerender) {
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
        while (head && content[begin] == head) {
          head = head.nextSibling;
          begin++;
        }

        // Find unchanged tail.
        let tail = this.lastChild;
        while (tail != head && content[end - 1] == tail) {
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

  // Attach method to event on selected element.
  attach(method, event, selector) {
    this.bind(selector, event, method.bind(this));
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

    // Register style sheet for web component including super classes.
    let styles = new Array();
    let c = cls;
    while (c && c != Component) {
      if (c.stylesheet) styles.push(c.stylesheet());
      c = Object.getPrototypeOf(c);
    }
    if (styles.length > 0) {
      let css = styles.reverse().join("\n").replace(/\$/g, tagname);
      stylesheet(css);
    }
  }
}

// Add global style sheet to document.
export function stylesheet(css) {
  let node = document.createElement("style");
  node.appendChild(document.createTextNode(css));
  if (document.head) document.head.appendChild(node);
}


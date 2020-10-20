// Web component framework.

// Base class for web components.
export class Component extends HTMLElement {
  // Initialize new web component.
  constructor() {
    super();
    this.state = null;
    this.props = {};
    this.elements = [...this.children];
    this.nodes = [...this.childNodes];
  }

  // Connect web component to DOM.
  connectedCallback () {
    // Add attributes to properties.
    for (const attr of this.attributes) {
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

      this.props[attr.name] = value;
    }

    // Render component.
    this.onconnect && this.onconnect();
    if (this.render) {
      this.innerHTML = this.render();
    }
    this.onconnected && this.onconnected();
  }

  // Update component state.
  update(state) {
    this.state = state || this.state;
    this.onupdate && this.onupdate();
    if (this.render) {
      this.innerHTML = this.render();
    }
    this.onupdated && this.onupdated();
  }

  // Generate HTML for template.
  html(strings, ...values) {
    let parts = [];
    for (let i = 0; i < strings.length; ++i) {
      parts.push(strings[i]);
      if (i >= values.length) continue;
      parts.push(values[i].toString());
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
    elem.addEventListener(event, handler);
  }

  // HTML string escape.
  static htmlmap = {
    '"': '&quot;',
    '&': '&amp;',
    '\'': '&#x27;',
    '<': '&lt;',
    '>': '&gt;',
    '`': '&#x60;',
    '/': '&#x2F;'
  }

  static escape(s) {
    return s.replace(/["&'<>`/]/g, c => Component.htmlmap[c]);
  }

  // Register class as a HTML web component.
  static register(cls, tagname) {
    // Compute tag name from class name if no tag name provided.
    if (!tagname) {
      tagname = cls.name.match(/[A-Z][a-z]*/g).join("-").toLowerCase();
    }

    // Check is tag name has already been registered.
    if (window.customElements.get(tagname)) return;

    // Register custom element.
    window.customElements.define(tagname, cls);

    // Register global style sheet.
    if (cls.stylesheet) {
      if (!Component.stylesheets.includes(cls.stylesheet)) {
        Component.stylesheets.push(cls.stylesheet);
        stylesheet(cls.stylesheet());
      }
    }
  }

  // Registered style sheet functions.
  static stylesheets = [];
}

// Add global style sheet to document.
export function stylesheet(css) {
  let node = document.createElement("style");
  node.appendChild(document.createTextNode(css));
  if (document.head) document.head.appendChild(node);
}


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
      let name = attr.name.replaceAll("-", "_");
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
    this.generate();
    this.onconnected && this.onconnected();
  }

  // Update component state.
  update(state) {
    this.state = state;
    let show = true;
    if (this.visible) show = this.visible();
    if (show) {
      this.onupdate && this.onupdate();
      this.generate();
      this.onupdated && this.onupdated();
    } else {
      this.style.display = "none";
    }
  }

  // Generate component content.
  generate() {
    let show = true;
    if (this.visible) show = this.visible();
    if (show) {
      this.style.display = "";
      if (this.render) {
        let content = this.render();
        if (content instanceof Array) {
          while (this.firstChild) this.removeChild(this.lastChild);
          for (let n of content) this.appendChild(n);
        } else if (content instanceof Node) {
          while (this.firstChild) this.removeChild(this.lastChild);
          this.appendChild(content);
        } else {
          this.innerHTML = content;
        }
      }
    } else {
      this.style.display = "none";
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

    // Register style sheet for web component.
    if (cls.stylesheet) {
      if (!Component.stylesheets.includes(cls.stylesheet)) {
        Component.stylesheets.push(cls.stylesheet);
        let css = cls.stylesheet().replaceAll("$", tagname);
        stylesheet(css);
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


// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Web component for selecting active subcomponent.
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


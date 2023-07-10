// Copyright 2024 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {DocumentViewer} from "/common/lib/docviewer.js";

class SideBar extends Component {
  visible() { return this.state; }

  onrendered() {
    this.attach(this.onmenu, "docmenu");
    this.attach(this.onresizedown, "pointerdown", "#sidebar-resizer");
    this.attach(this.onresizeup, "pointerup", "#sidebar-resizer");
    this.attach(this.onresizemove, "pointermove", "#sidebar-resizer");
    this.attach(this.onclose, "click", "#close");
  }

  onclose(e) {
    this.update(null);
  }

  onresizedown(e) {
    let resizer = e.target;
    resizer.setPointerCapture(e.pointerId);
    this.x = e.clientX;
    this.width = this.offsetWidth;
    this.capture = true;
  }

  onresizeup(e) {
    this.capture = false;
  }

  onresizemove(e) {
    if (!this.capture) return;
    let offset = this.x - e.clientX;
    this.style.width = `${this.width + offset}px`;
  }

  onmenu(e) {
    let command = e.detail;
    if (command == "close") {
      this.update(null);
    }
  }

  onupdated() {
    this.find("document-viewer").update(this.state);
  }

  render() {
    return `
      <div id="sidebar-resizer"></div>
      <document-viewer></document-viewer>
      <md-icon-button id="close" icon="keyboard_arrow_right"></md-icon-button>
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        flex-direction: row;
        position: relative;
        overflow: auto;
        width: 30vw;
        max-width: 10vw;
        max-width: 70vw;
        border-left: 1px #a0a0a0 solid;
      }
      $ document-viewer {
        padding: 16px 16px 16px 8px;
        width: 100%;
      }
      $ #sidebar-resizer {
        cursor: col-resize;
        width: 8px;
      }
      $ #close {
        position: absolute;
        right: 4px;
        top: 4px;
      }
    `;
  }

}

Component.register(SideBar);


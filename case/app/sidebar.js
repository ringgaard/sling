// Copyright 2024 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {DocumentViewer} from "/common/lib/docviewer.js";

function same(d1, d2) {
  if (!d1 || !d2) return false;
  if (d1 == d2) return true;
  if (d1.context.topic != d2.context.topic) return false;
  if (d1.context.index != d2.context.index) return false;
  return true;
}

class SideBar extends Component {
  visible() { return this.state; }

  onrendered() {
    this.attach(this.onresizedown, "pointerdown", "#sidebar-left");
    this.attach(this.onresizeup, "pointerup", "#sidebar-left");
    this.attach(this.onresizemove, "pointermove", "#sidebar-left");
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

  onupdated() {
    this.find("document-viewer").update(this.state);
  }

  onrefresh(newdoc) {
    if (!same(this.state, newdoc)) return;
    let viewer = this.find("document-viewer");
    let scroll = viewer.scrollTop;
    this.state = newdoc;
    viewer.update(newdoc);
    viewer.scrollTop = scroll;
  }

  ondelete(doc) {
    if (same(this.state, doc)) this.update(null);
  }

  render() {
    return `
      <div id="sidebar-left">
        <md-icon id="close" icon="keyboard_arrow_right"></md-icon>
      </div>
      <document-viewer></document-viewer>
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        flex-direction: row;
        position: relative;
        width: 30vw;
        max-width: 10vw;
        max-width: 70vw;
        border-left: 1px #a0a0a0 solid;
      }
      $ document-viewer {
        width: 100%;
        overflow: auto;
      }
      $ #sidebar-left {
        display: flex;
        flex-direction: column;
        justify-content: flex-end;
        cursor: col-resize;
        width: 16px;
        color: white;
      }
      $ #sidebar-left:hover {
        color: #a0a0a0;
      }
      $ #close {
        cursor: e-resize;
      }
    `;
  }
}

Component.register(SideBar);


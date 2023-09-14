// Copyright 2024 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Frame} from "/common/lib/frame.js";
import {store, frame} from "/common/lib/global.js";
import {Component} from "/common/lib/component.js";
import {Document} from "/common/lib/document.js";
import {inform} from "/common/lib/material.js";
import {DocumentViewer} from "/common/lib/docviewer.js";

const n_name = frame("name");
const n_lex = frame("lex");

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
    this.tabIndex = 0;
    this.attach(this.onresizedown, "pointerdown", "#sidebar-left");
    this.attach(this.onresizeup, "pointerup", "#sidebar-left");
    this.attach(this.onresizemove, "pointermove", "#sidebar-left");
    this.attach(this.onkeydown, "keydown");

    if (this.state) {
      let context = this.state.context;
      let source = this.state.source;
      let tocname = context.topic.get(n_name);
      let docname;
      if (source instanceof Frame) {
        docname = source.get(n_name);
      }
      this.find("#tocname").update(tocname);
      this.find("#docname").update(docname);
      this.attach(this.onmenu, "select", "md-menu");
      this.attach(this.onnavigate, "click", "#titlebox");
    }
  }

  async onkeydown(e) {
    if (e.ctrlKey && e.code == "KeyE") {
      e.preventDefault();
      this.onedit();
    }
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

  async onedit() {
    let editor = this.match("#editor");
    let card = await editor.navigate_to(this.state.context.topic);
    card.dispatch("docmenu", {command: "edit", document: this.state}, true);
  }

  onnext() {
    let context = this.state.context;
    let topic = context.topic;
    let index = context.index + 1;
    let source = context.topic.value(context.topic.slot(n_lex, index));
    if (source) {
      this.update(new Document(store, source, {topic, index}));
    } else {
      inform("already at end");
    }
  }

  onprev() {
    let context = this.state.context;
    let topic = context.topic;
    let index = context.index - 1;
    let source = context.topic.value(context.topic.slot(n_lex, index));
    if (source) {
      this.update(new Document(store, source, {topic, index}));
    } else {
      inform("already at beginning");
    }
  }

  async onmenu(e) {
    let command = e.target.id;
    let document = this.state;

    if (command == "close") {
      this.update(null);
    } else if (command == "next") {
      this.onnext();
    } else if (command == "prev") {
      this.onprev();
    } else {
      let editor = this.match("#editor");
      let card = await editor.navigate_to(this.state.context.topic);
      card.dispatch("docmenu", {command, document}, true);
    }
  }

  async onnavigate(e) {
    let editor = this.match("#editor");
    await editor.navigate_to(this.state.context.topic);
  }

  async onupdate() {
    if (!this.state) return;
    let editor = this.match("#editor");
    if (!editor) return;
    let ok = await editor.check_rights(this.state.context.topic);
    if (!ok) this.state = null;
  }

  onupdated() {
    let viewer = this.find("document-viewer");
    if (viewer) viewer.update(this.state);
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
      <div id="sidebar-left"></div>
      <div id="main">
        <div id="banner">
          <div id="titlebox">
            <md-text id="tocname"></md-text><br>
            <md-text id="docname"></md-text>
          </div>
          <md-menu id="menu">
            <md-menu-item id="edit">Edit</md-menu-item>
            <md-menu-item id="next">Next</md-menu-item>
            <md-menu-item id="prev">Previous</md-menu-item>
            <md-menu-item id="analyze">Analyze</md-menu-item>
            <md-menu-item id="phrasematch">Match phrases</md-menu-item>
            <md-menu-item id="topicmatch">Match topics</md-menu-item>
            <md-menu-item id="close">Close</md-menu-item>
          </md-menu>
        </div>
        <document-viewer></document-viewer>
      </div>
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        flex-direction: row;
        position: relative;
        width: 30vw;
        min-width: 10vw;
        max-width: 70vw;
        border-left: 1px #a0a0a0 solid;
      }
      $ #main {
        display: flex;
        flex-direction: column;
        overflow: hidden;
        width: 100%;
      }
      $ #banner {
        display: flex;
        flex-direction: row;
        padding: 4px 4px 4px 0px;
      }
      $ #titlebox {
        flex: 1 1 auto;
        overflow: hidden;
        white-space: nowrap;
        color: #808080;
        cursor: pointer;
      }
      $ #tocname {
        overflow: hidden;
        font-size: 12px;
      }
      $ #docname {
        overflow: hidden;
        font-size: 16px;
        font-weight: bold;
      }
      $ #menu {
        flex: none;
      }
      $ document-viewer {
        width: 100%;
        height: 100%;
        overflow: auto;
        padding: 0;
      }
      $ #sidebar-left {
        cursor: col-resize;
        min-width: 16px;
      }
    `;
  }
}

Component.register(SideBar);


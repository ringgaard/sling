// Copyright 2023 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {Document} from "/common/lib/document.js";
import {store, frame} from "/common/lib/global.js";
import {NewFolderDialog} from "./folder.js";


const n_lex = frame("lex");

class DrawerPanel extends Component {
  onrendered() {
    if (!this.state) return;
    this.editor = this.match("#editor");
    this.attach(this.ondrawerdown, "pointerdown", "#resizer");
    this.attach(this.ondrawerup, "pointerup", "#resizer");
    this.attach(this.ondrawermove, "pointermove", "#resizer");
    if (this.state.folders) {
      this.attach(this.onnewfolder, "click", "#newfolder");
    } else {
      this.attach(this.onclose, "click", "#close");
    }
  }

  onupdated() {
    if (!this.state) return;
    if (this.state.folders) {
      this.find("folder-list").update(this.state);
    } else {
      this.find("index-entry").update(this.state);
    }
  }

  toogle() {
    this.hidden = !this.hidden;
    this.style.display = this.hidden ? "none" : "flex";
  }

  onclose(e) {
    this.editor.update_folders();
  }

  ondrawerdown(e) {
    let resizer = e.target;
    resizer.setPointerCapture(e.pointerId);
    this.drawer_x = e.clientX;
    this.drawer_capture = true;
  }

  ondrawerup(e) {
    this.drawer_capture = false;
  }

  ondrawermove(e) {
    if (!this.drawer_capture) return;
    let offset = e.clientX - this.drawer_x;
    this.style.width = `${this.offsetWidth + offset}px`;
    this.drawer_x = e.clientX;
  }

  async onnewfolder(e) {
    if (this.editor.readonly) return;
    let dialog = new NewFolderDialog();
    let result = await dialog.show();
    if (result) {
      this.editor.add_folder(result);
    }
  }

  render() {
    if (!this.state) return;
    let h = new Array();
    if (this.state.folders) {
      h.push(`
        <div id="folders">
          <div class="top">
            Folders
            <md-spacer></md-spacer>
            <md-icon-button
              id="newfolder"
              icon="create_new_folder"
              tooltip="Create new folder"
              tooltip-align="right">
            </md-icon-button>
          </div>
          <folder-list></folder-list>
        </div>`);
    } else {
      h.push(`
        <div id="index">
          <div class="top">
            Index
            <md-spacer></md-spacer>
            <md-icon-button
              id="close"
              icon="close"
              tooltip="Close index"
              tooltip-align="right">
            </md-icon-button>
          </div>
          <index-entry></index-entry>
        </div>`);
    }
    h.push('<div id="resizer"></div>');
    return h.join("");
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        flex-direction: row;
        justify-content: stretch;
        width: 150px;
        height: 100%;
        min-width: 100px;
        padding: 3px 0px 3px 3px;
        box-sizing: border-box;
        overflow-x: clip;
        overflow-y: auto;
      }
      $ md-icon {
        color: #808080;
      }
      $ md-icon-button {
        color: #808080;
      }
      $ #resizer {
        cursor: col-resize;
        flex: 0 0 3px;
        z-index: 2;
      }
      $ .top {
        display: flex;
        align-items: center;
        font-size: 16px;
        font-weight: bold;
        margin-left: 6px;
        border-bottom: thin solid #808080;
        margin-bottom: 6px;
        min-height: 40px;
      }
      $ #folders {
        flex: 1 1 auto;
        width: 100%;
      }
      $ #index {
        flex: 1 1 auto;
        width: 100%;
      }
    `;
  }
}

Component.register(DrawerPanel);

class IndexEntry extends Component {
  onrendered() {
    if (this.state) {
      this.attach(this.onnavigate, "click", ".name");
      if (this.state.entries) {
        this.attach(this.onexpand, "click", "md-icon");
        let list = this.find("entry-list");
        if (list) list.update(this.state.entries);
      }
    }
  }

  onexpand(e) {
    this.state.open = !this.state.open;
    this.update(this.state);
  }

  onnavigate(e) {
    let entry = this.state;
    if (entry.topic) {
      this.dispatch("navigate", {ref: entry.topic.id, event: e}, true);
    } else if (entry.context) {
      let book = entry.context.book;
      let index = entry.context.index;
      let source = book.value(book.slot(n_lex, index));
      let context = {topic: book, index: index, match: entry.item};
      let doc = new Document(store, source, context);

      let sidebar = document.getElementById("sidebar");
      sidebar.goto(doc);
    }
  }

  render() {
    let entry = this.state;
    if (!entry) return;

    let h = new Array();
    h.push('<div class="entry">');
    if (!entry.entries) {
      h.push('<md-icon></md-icon>');
    } else if (entry.open) {
      h.push('<md-icon icon="arrow_drop_down"></md-icon>');
    } else {
      h.push('<md-icon icon="arrow_right"></md-icon>');
    }
    h.push(`<span class="name">${Component.escape(entry.name)}</span>`);
    h.push('</div>');
    if (entry.open) {
      h.push(`<entry-list></entry-list>`);
    }
    return h.join("");
  }

  static stylesheet() {
    return `
      $ {
      }
      $ div.entry {
        display: flex;
        align-items: center;
        cursor: pointer;
      }
      $ div.entry:hover {
        background-color: #eeeeee;
      }
      $ md-icon {
        width: 20px;
      }
      $ span.name {
        overflow-x: clip;
        white-space: nowrap;
      }
    `;
  }
}

Component.register(IndexEntry);

class EntryList extends Component {
  render() {
    let entries = this.state;
    if (!entries) return;

    let h = new Array();
    for (let entry of this.state) {
      h.push(new IndexEntry(entry));
    }
    return h;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        flex-direction: column;
        padding-left: 16px;
      }
    `;
  }
}

Component.register(EntryList);


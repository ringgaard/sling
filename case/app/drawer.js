// Copyright 2023 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {MdDialog} from "/common/lib/material.js";
import {Document} from "/common/lib/document.js";
import {store, frame} from "/common/lib/global.js";

const n_lex = frame("lex");

class DrawerPanel extends Component {
  onrendered() {
    if (!this.state) return;
    this.editor = this.match("#editor");
    this.attach(this.onnewfolder, "click", "#newfolder");
    if (this.index) this.attach(this.onclose, "click", "#close");
  }

  onupdate() {
    let content = this.find("#content");
    this.scroll = content && content.scrollTop;
  }

  onupdated() {
    if (!this.state) return;
    this.find("folder-list").update(this.state);
    if (this.index) this.find("index-entry").update(this.index);
    let content = this.find("#content");
    content.scrollTop = this.scroll;
  }

  toogle() {
    this.hidden = !this.hidden;
    this.style.display = this.hidden ? "none" : "flex";
  }

  set_index(index) {
    this.index = index;
    this.update(this.state);
  }

  onclose(e) {
    this.set_index(null);
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
    h.push('<div id="content">')

    h.push(`
      <div class="top">Folders
        <md-spacer></md-spacer>
        <md-icon-button
          id="newfolder"
          icon="create_new_folder"
          tooltip="Create new folder"
          tooltip-align="right">
        </md-icon-button>
      </div>
      <folder-list></folder-list>`);

   if (this.index) {
      h.push(`
        <div class="top">Index
          <md-spacer></md-spacer>
          <md-icon-button
            id="close"
            icon="close"
            tooltip="Close index"
            tooltip-align="right">
          </md-icon-button>
        </div>
        <index-entry></index-entry>`);
    }
    h.push('</div>');
    h.push('<md-resizer class="right"></md-resizer>');
    return h.join("");
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        position: relative;
        width: 150px;
        min-width: 100px;
      }
      $ md-icon {
        color: #808080;
      }
      $ md-icon-button {
        color: #808080;
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
      $ #content {
        width: 100%;
        height: 100%;
        overflow-x: clip;
        overflow-y: auto;
      }
    `;
  }
}

Component.register(DrawerPanel);

class FolderList extends Component {
  render() {
    if (!this.state) return;
    let folders = this.state.folders;
    let current = this.state.current;
    let scraps = this.state.scraps;
    let readonly = this.state.readonly
    let h = [];
    for (let [name, folder] of folders) {
      let marked = folder == current;
      h.push(new CaseFolder({name, folder, readonly, marked}));
    }
    if (scraps.length > 0) {
      h.push(new ScrapsFolder({folder: scraps, marked: scraps == current}));
    }
    return h;
  }
}

Component.register(FolderList);

class CaseFolder extends Component {
  onconnected() {
    this.bind(null, "click", e => this.onclick(e));
    if (!this.state.readonly) {
      this.bind("#rename", "select", e => this.onrename(e));
      this.bind("#moveup", "select", e => this.onmoveup(e));
      this.bind("#movedown", "select", e => this.onmovedown(e));
      this.bind("#delete", "select", e => this.ondelete(e));
    }
  }

  onclick(e) {
    this.match("#editor").show_folder(this.state.folder);
  }

  async onrename(e) {
    let dialog = new RenameFolderDialog(this.state.name);
    let result = await dialog.show();
    if (result) {
      this.match("#editor").rename_folder(this.state.folder, result);
    }
  }

  onmoveup(e) {
    this.match("#editor").move_folder_up(this.state.folder);
  }

  onmovedown(e) {
    this.match("#editor").move_folder_down(this.state.folder);
  }

  ondelete(e) {
    this.match("#editor").delete_folder(this.state.folder);
  }

  render() {
    let menu = `
      <md-menu>
        <md-menu-item id="rename">
          <md-icon icon="drive_file_rename_outline"></md-icon>Rename
        </md-menu-item>
        <md-menu-item id="moveup">
          <md-icon icon="move_up"></md-icon>Move up
        </md-menu-item>
        <md-menu-item id="movedown">
          <md-icon icon="move_down"></md-icon>Move down
        </md-menu-item>
        <md-menu-item id="delete">
         <md-icon icon="delete"></md-icon>Delete
        </md-menu-item>
      </md-menu>
    `;

    return `
      <md-icon icon="folder"></md-icon>
      <div ${this.state.marked ? 'class="current"' : ''}>
        ${Component.escape(this.state.name)}
      </div>
      ${this.state.readonly ? "" : menu}
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        align-items: center;
        padding-left: 6px;
        margin-right: 3px;
        white-space: nowrap;
        cursor: pointer;
        height: 30px;
        border-radius: 15px;
        border: 0;
        fill: #808080;
      }
      $:hover {
        background-color: #eeeeee;
      }
      $ md-menu #open {
        display: none;
        height: 30px;
        width: 30px;
        border-radius: 15px;
      }
      $:hover md-menu #open {
        display: flex;
      }
      $:hover {
        background-color: #eeeeee;
      }
      $ div {
        padding-left: 6px;
        user-select: none;
        overflow-x: hidden;
        flex: 1;
      }
      $ div.current {
        font-weight: bold;
        padding-left: 6px;
      }
    `;
  }
}

Component.register(CaseFolder);

class ScrapsFolder extends CaseFolder {
  onconnected() {
    this.bind(null, "click", e => this.onclick(e));
    this.bind("#clear", "select", e => this.onclear(e));
  }

  async onclear(e) {
    let scraps = this.state.folder;
    let editor = this.match("#editor");
    if (editor.folder != scraps) {
      await editor.show_folder(scraps);
    }
    await editor.delete_topics([...scraps]);
  }

  render() {
    return `
      <md-icon icon="folder_delete"></md-icon>
      <div ${this.state.marked ? 'class="current"' : ''}>Scraps</div>
      <md-menu>
        <md-menu-item id="clear">
         <md-icon icon="delete_forever"></md-icon>Clear</md-menu-item>
      </md-menu>
    `;
  }
}

Component.register(ScrapsFolder);

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
    let title = Component.escape(entry.name);
    if (entry.count > 1) title += " (" + entry.count + ")";
    h.push(`<span class="name">${title}</span>`);
    h.push('</div>');
    if (entry.open) {
      h.push(`<entry-list></entry-list>`);
    }
    return h.join("");
  }

  static stylesheet() {
    return `
      $ {
        padding-right: 3px;
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
        flex: 0 0 20px;
      }
      $ span.name {
        flex: 1 1 auto;
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
        padding-left: 10px;
      }
    `;
  }
}

Component.register(EntryList);

export class NewFolderDialog extends MdDialog {
  submit() {
    this.close(this.find("#name").value.trim());
  }

  render() {
    let p = this.state;
    return `
      <md-dialog-top>Create new case folder</md-dialog-top>
      <div id="content">
        <md-text-field
          id="name"
          value="${Component.escape(this.state)}"
          label="Folder name">
        </md-text-field>
      </div>
      <md-dialog-bottom>
        <button id="cancel">Cancel</button>
        <button id="submit">Create</button>
      </md-dialog-bottom>
    `;
  }

  static stylesheet() {
    return `
      $ #content {
        display: flex;
        flex-direction: column;
        row-gap: 16px;
      }
      $ #name {
        width: 300px;
      }
    `;
  }
}

Component.register(NewFolderDialog);

class RenameFolderDialog extends MdDialog {
  submit() {
    this.close(this.find("#name").value.trim());
  }

  render() {
    let p = this.state;
    return `
      <md-dialog-top>Rename case folder</md-dialog-top>
      <div id="content">
        <md-text-field
          id="name"
          value="${Component.escape(this.state)}"
          label="Folder name">
        </md-text-field>
      </div>
      <md-dialog-bottom>
        <button id="cancel">Cancel</button>
        <button id="submit">Rename</button>
      </md-dialog-bottom>
    `;
  }

  static stylesheet() {
    return `
      $ #content {
        display: flex;
        flex-direction: column;
        row-gap: 16px;
      }
      $ #name {
        width: 300px;
      }
    `;
  }
}

Component.register(RenameFolderDialog);


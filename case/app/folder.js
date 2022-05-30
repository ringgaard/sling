// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {MdDialog} from "/common/lib/material.js";

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
      <md-spacer></md-spacer>
      <md-menu>
        <md-menu-item id="rename">
          <md-icon icon="drive_file_rename_outline"></md-icon>Rename
        </md-menu-item>
        <md-menu-item id="moveup">
          <md-icon icon="move-up"></md-icon>Move up
        </md-menu-item>
        <md-menu-item id="movedown">
          <md-icon icon="move-down"></md-icon>Move down
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
        white-space: nowrap;
        cursor: pointer;
        height: 30px;
        border-radius: 15px;
        border: 0;
        fill: #808080;
      }
      $ md-icon-button button {
        height: 30px;
        width: 30px;
        border-radius: 15px;
      }
      $:hover {
        background-color: #eeeeee;
      }
      $ md-menu #open {
        visibility: hidden;
      }
      $:hover md-menu #open {
        visibility: visible;
      }
      $:hover {
        background-color: #eeeeee;
      }
      $ div {
        padding-left: 6px;
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
      <md-spacer></md-spacer>
      <md-menu>
        <md-menu-item id="clear">
         <md-icon icon="delete_forever"></md-icon>Clear</md-menu-item>
      </md-menu>
    `;
  }
}

Component.register(ScrapsFolder);

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


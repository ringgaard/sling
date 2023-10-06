// Copyright 2023 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {NewFolderDialog} from "./folder.js";

class DrawerPanel extends Component {
  onrendered() {
    if (!this.state) return;
    this.editor = this.match("#editor");
    this.attach(this.ondrawerdown, "pointerdown", "#resizer");
    this.attach(this.ondrawerup, "pointerup", "#resizer");
    this.attach(this.ondrawermove, "pointermove", "#resizer");
    if (this.state.folders) {
      this.attach(this.onnewfolder, "click", "#newfolder");
    }
    if (this.state.entries) {
      this.attach(this.onclose, "click", "#close");
    }
  }

  onupdated() {
    if (!this.state) return;
    if (this.state.folders) {
      this.find("folder-list").update(this.state);
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
    if (this.state.entries) {
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
        </div>`);
    } else {
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
    }
    h.push('<div id="resizer"></div>');
    return h.join("");
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        flex-direction: row;
        width: 150px;
        height: 100%;
        min-width: 100px;
        padding: 3px 0px 3px 3px;
        overflow: auto;
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
        width: 3px;
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
        width: 100%;
        height: 100%;
      }
      $ #index {
        width: 100%;
        height: 100%;
      }
    `;
  }
}

Component.register(DrawerPanel);


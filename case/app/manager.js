// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import * as material from "/common/lib/material.js";
import {store, settings, save_settings} from "./global.js";

function date2str(date) {
  return date.toLocaleString();
}

//-----------------------------------------------------------------------------
// Case Manager
//-----------------------------------------------------------------------------

class CaseManager extends Component {
  onconnected() {
    this.bind("#settings", "click", e => this.onsettings(e));
  }

  onupdate() {
    if (!this.state) return;
    this.find("case-list").update(this.state);
  }

  onsettings(e) {
    let s = this.find("settings-panel");
    if (this.find("md-drawer").toogle()) {
      s.onopen();
    } else {
      s.onclose();
      location.reload();
    }
  }

  prerender() {
    return `
      <md-column-layout>
        <md-toolbar>
          <md-toolbar-logo></md-toolbar-logo>
          <div>Case</div>
          <case-search-box id="search"></case-search-box>
          <md-spacer></md-spacer>
          <md-icon-button id="settings" icon="settings"></md-icon-button>
        </md-toolbar>

        <md-row-layout>
          <md-content>
            <case-list></case-list>
          </md-content>
          <md-drawer id="settings">
            <settings-panel></settings-panel>
          </md-drawer>
        </md-row-layout>
      </md-column-layout>

      </md-column-layout>
    `;
  }

  static stylesheet() {
    return `
      $ md-row-layout {
        overflow: auto;
        height: 100%;
      }
      $ md-drawer {
        padding: 10px;
      }
    `;
  }
}

Component.register(CaseManager);

//-----------------------------------------------------------------------------
// New case
//-----------------------------------------------------------------------------

class NewCaseDialog extends material.MdDialog {
  submit() {
    this.close({
      name: this.find("#name").value.trim(),
      description: this.find("#description").value.trim(),
    });
  }

  render() {
    let p = this.state;
    return `
      <md-dialog-top>Open new case</md-dialog-top>
      <div id="content">
        <md-text-field
          id="name"
          value="${Component.escape(p.name)}"
          label="Name">
        </md-text-field>
        <md-text-field
          id="description"
          value="${Component.escape(p.description)}"
          label="Description">
        </md-text-field>
      </div>
      <md-dialog-bottom>
        <button id="cancel">Cancel</button>
        <button id="submit">Create</button>
      </md-dialog-bottom>
    `;
  }

  static stylesheet() {
    return material.MdDialog.stylesheet() + `
      $ #content {
        display: flex;
        flex-direction: column;
        row-gap: 16px;
      }
      $ #name {
        width: 300px;
      }
      $ #description {
        width: 500px;
      }
    `;
  }
}

Component.register(NewCaseDialog);

//-----------------------------------------------------------------------------
// Case search box
//-----------------------------------------------------------------------------

class CaseSearchBox extends Component {
  onconnected() {
    this.bind("#add", "click", e => this.onadd(e));
    this.bind("md-search", "query", e => this.onquery(e));
    this.bind("md-search", "item", e => this.onitem(e));
    this.bind("md-search", "enter", e => this.onenter(e));
  }

  async onquery(e) {
    let detail = e.detail
    let target = e.target;
    let query = detail.trim();

    // Do full match if query ends with period.
    let full = false;
    if (query.endsWith(".")) {
      full = true;
      query = query.slice(0, -1);
    }

    // Seach local case database.
    let items = [];
    let seen = new Set();
    for (let result of app.search(query, full)) {
      items.push(new material.MdSearchResult({
        ref: result.id,
        name: result.name,
        title: result.name + " ⭐",
        description: result.description,
        caserec: result,
      }));
      seen.add("c/" + result.id);
    }

    // Seach knowledge base for matches for new case.
    try {
      let params = "fmt=cjson";
      if (full) params += "&fullmatch=1";
      params += `&q=${encodeURIComponent(query)}`;
      let response = await fetch(`${settings.kbservice}/kb/query?${params}`);
      let data = await response.json();
      for (let item of data.matches) {
        if (seen.has(item.ref)) continue;
        items.push(new material.MdSearchResult({
          ref: item.ref,
          name: item.text,
          description: item.description,
        }));
      }
    } catch (error) {
      console.log("Case query error", query, error.message, error.stack);
      material.StdDialog.error(error.message);
      target.populate(detail, null);
      return;
    }

    target.populate(detail, items);
  }

  onitem(e) {
    let item = e.detail;
    if (item.caserec) {
      app.open_case(item.caserec.id);
    } else if (item.ref.startsWith("c/")) {
      let caseid = parseInt(item.ref.substring(2));
      app.open_case(caseid);
    } else {
      let dialog = new NewCaseDialog({name: item.name});
      dialog.show().then(result => {
        if (result && result.name) {
          app.add_case(result.name, result.description, item.ref);
        }
      });
    }
  }

  onenter(e) {
    let name = e.detail;
    let dialog = new NewCaseDialog({name});
    dialog.show().then(result => {
      if (result) {
        app.add_case(result.name, result.description, null);
      }
    });
  }

  onadd(e) {
    e.preventDefault();
    let name = this.query();
    let dialog = new NewCaseDialog({name});
    dialog.show().then(result => {
      if (result) {
        app.add_case(result.name, result.description, null);
      }
    });
  }

  query() {
    return this.find("md-search").query();
  }

  clear() {
    return this.find("md-search").clear();
  }

  render() {
    return `
      <form>
        <md-search
          placeholder="Search for case or topic..."
          min-length=2>
        </md-search>
        <md-icon-button id="add" icon="add"></md-icon-button>
      </form>
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: block;
        width: 100%;
        max-width: 800px;
        padding-left: 10px;
        padding-right: 3px;
      }
      $ form {
        display: flex;
        width: 100%;
      }
    `;
  }
}

Component.register(CaseSearchBox);

//-----------------------------------------------------------------------------
// Case list
//-----------------------------------------------------------------------------

class CaseList extends material.MdCard {
  onupdated() {
    this.bind("table", "click", e => this.onclick(e));
    this.bind("table", "mousedown", e => this.ondown(e));
  }

  ondown(e) {
    this.ofsx = e.offsetX;
    this.ofsy = e.offsetY;
  }

  onclick(e) {
    // Ignore if selecting text.
    let dx = this.ofsx - e.offsetX;
    let dy = this.ofsy - e.offsetY;
    if (Math.abs(dx) + Math.abs(dy) > 10) return;

    // Get case number for clicked row.
    let row = e.target.closest("tr");
    let button = e.target.closest("md-icon-button");
    let caseid = row ? parseInt(row.getAttribute("case")) : undefined;
    let link = row ? row.getAttribute("link") == "true" : undefined;
    if (!caseid) return;

    if (button) {
      // Perform action on case.
      let action = button.getAttribute("icon");
      if (action == "delete") {
        let message = `Delete ${link ? "link to " : ""} case #${caseid}?`;
        material.StdDialog.confirm("Delete case", message, "Delete")
        .then(result => {
          if (result) {
            app.delete_case(caseid, link);
          }
        });
      }
    } else {
      // Open case.
      app.open_case(caseid);
    }
  }

  render() {
   if (!this.state) return null;
   let h = [];
   h.push("<table>");
   h.push(`
     <thead><tr>
       <th>Case #</th>
       <th></th>
       <th>Name</th>
       <th>Description</th>
       <th>Created</th>
       <th>Modified</th>
       <th></th>
     </tr></thead>
   `);
    h.push("<tbody>");
    for (let rec of this.state) {
      if (rec.nsfw && !settings.nsfw) continue;
      let icon = "";
      if (rec.link) {
        icon = '<md-icon icon="link" outlined></md-icon>';
      } else if (rec.publish) {
        icon = '<md-icon icon="public" outlined></md-icon>';
      } else if (rec.share) {
        icon = '<md-icon icon="share" outlined></md-icon>';
      }
      if (rec.share) {
        if (rec.shared && rec.shared < rec.modified) {
          icon += "*";
        }
      }
      h.push(`
        <tr case="${rec.id}" link="${rec.link}">
          <td>${rec.id}</td>
          <td><div>${icon}</div></td>
          <td>${Component.escape(rec.name)}</td>
          <td>${Component.escape(rec.description)}</td>
          <td>${date2str(rec.created)}</td>
          <td>${date2str(rec.modified)}</td>
          <td><md-icon-button icon="delete"></md-icon-button></td>
        </tr>
     `);
    }
    h.push("</tr>");
    return h.join("");
  }

  static stylesheet() {
    return material.MdCard.stylesheet() + `
      $ table {
        border: 0;
        white-space: nowrap;
        font-size: 16px;
        text-align: left;
        width: 100%;
      }

      $ thead {
        padding-bottom: 3px;
      }

      $ th {
        vertical-align: bottom;
        padding: 8px 12px;
        box-sizing: border-box;
        border-bottom: 1px solid rgba(0,0,0,.12);
        text-overflow: ellipsis;
        color: rgba(0,0,0,.54);
      }

      $ td {
        vertical-align: middle;
        border-bottom: 1px solid rgba(0,0,0,.12);
        padding: 0px 12px;
        box-sizing: border-box;
        text-overflow: ellipsis;
        overflow: hidden;
      }

      $ tbody>tr:hover {
        background-color: #eeeeee;
        cursor: pointer;
      }

      /* case# */
      $ td:nth-child(1) {
        text-align: right;
      }

      /* icon */
      $ td:nth-child(2) div {
        display: flex;
        align-items: center;
      }

      /* description */
      $ td:nth-child(4) {
        width: 100%;
        white-space: normal;
      }

      $ md-icon-button {
        color: #808080;
      }
    `;
  }
}

Component.register(CaseList);

//-----------------------------------------------------------------------------
// Settings panel
//-----------------------------------------------------------------------------

class SettingsPanel extends Component {
  onopen() {
    this.find("#picturesize").value = settings.picturesize;
    this.find("#kbservice").value = settings.kbservice;
    this.find("#imagesearch").checked = settings.imagesearch;
    this.find("#nsfw").checked = settings.nsfw;
  }

  onclose() {
    settings.picturesize = this.find("#picturesize").value;
    settings.kbservice = this.find("#kbservice").value;
    settings.imagesearch = this.find("#imagesearch").checked;
    settings.nsfw = this.find("#nsfw").checked;
    save_settings();
  }

  render() {
    return `
      <div id="settings-title">Settings</div>
      <hr>
      <div id="content">
        <md-text-field
          id="picturesize"
          label="Profile picture size">
        </md-text-field>
        <md-text-field
          id="kbservice"
          label="Knowledge service URL">
        </md-text-field>
        <md-switch id="imagesearch" label="Enable image search"></md-switch>
        <md-switch id="nsfw" label="Show adult content (NSFW)"></md-switch>
      </div>
    `;
  }

  static stylesheet() {
    return `
      $ {
        padding: 10px;
      }
      $ #settings-title {
        font-size: 20px;
        font-weight: bold;
      }
      $ #content {
        display: flex;
        flex-direction: column;
        row-gap: 16px;
      }
    `;
  }
}

Component.register(SettingsPanel);


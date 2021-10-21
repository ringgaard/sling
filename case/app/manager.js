// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import * as material from "/common/lib/material.js";
import {store, settings} from "./global.js";

const kbservice = "https://ringgaard.com"

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
    this.find("md-drawer").toogle();
  }

  prerender() {
    return `
      <md-column-layout>
        <md-toolbar>
          <md-toolbar-logo></md-toolbar-logo>
          <div>Cases</div>
          <case-search-box id="search"></case-search-box>
          <md-spacer></md-spacer>
          <md-icon-button id="settings" icon="settings"></md-icon-button>
        </md-toolbar>

        <md-row-layout>
          <md-content>
            <case-list></case-list>
          </md-content>
          <md-drawer>
            <div id="settings-title">Settings</div>
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
      $ #settings-title {
        font-size: 20px;
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

  onquery(e) {
    let detail = e.detail
    let target = e.target;
    let params = "fmt=cjson";
    let query = detail.trim();
    if (query.endsWith(".")) {
      params += "&fullmatch=1";
      query = query.slice(0, -1);
    }
    params += `&q=${encodeURIComponent(query)}`;

    fetch(`${settings.kbservice}/kb/query?${params}`)
    .then(response => response.json())
    .then((data) => {
      let items = [];
      for (let item of data.matches) {
        items.push(new material.MdSearchResult({
          ref: item.ref,
          name: item.text,
          description: item.description
        }));
      }
      target.populate(detail, items);
    })
    .catch(error => {
      console.log("Query error", query, error.message, error.stack);
      StdDialog.error(error.message);
      target.populate(detail, null);
    });
  }

  onitem(e) {
    let item = e.detail;
    let dialog = new NewCaseDialog({name: item.name});
    dialog.show().then(result => {
      if (result && result.name) {
        app.add_case(result.name, result.description, item.ref);
      }
    });
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
          min-length=2
          autofocus>
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
       <th>Name</th>
       <th>Description</th>
       <th>Created</th>
       <th>Modified</th>
       <th></th>
     </tr></thead>
   `);
    h.push("<tbody>");
    for (let rec of this.state) {
      let icon = "";
      if (rec.link) {
        icon = '<md-icon icon="link" outlined></md-icon>';
      } else if (rec.publish) {
        icon = '<md-icon icon="public" outlined></md-icon>';
      } else if (rec.share) {
        icon = '<md-icon icon="share" outlined></md-icon>';
      }
      h.push(`
        <tr case="${rec.id}" link="${rec.link}">
          <td>${rec.id}</td>
          <td>
            <div>
              <span>${Component.escape(rec.name)}</span>
              ${icon}
            </div>
          </td>
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

      $ td:nth-child(1) { /* case# */
        text-align: right;
      }

      /* name */
      $ td:nth-child(2) div {
        display: flex;
        align-items: center;
      }
      $ td:nth-child(2) md-icon {
        padding-left: 6px;
      }

      /* description */
      $ td:nth-child(3) {
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


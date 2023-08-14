// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import * as material from "/common/lib/material.js";
import {store, settings, save_settings} from "/common/lib/global.js";

import {search, kbsearch} from "./search.js";

function pad2(num) {
  return ("0" + num).toString().slice(-2)
}

function pad4(num) {
  return ("000" + num).toString().slice(-4)
}

function date2str(date, full=true) {
  if (!date) return "";
  if (typeof(date) === 'string') date = new Date(date);
  let year = pad4(date.getFullYear());
  let month = pad2(date.getMonth() + 1);
  let day = pad2(date.getDate());
  let hours = pad2(date.getHours());
  let mins = pad2(date.getMinutes());
  if (full) {
    return `${year}-${month}-${day} ${hours}:${mins}`;
  } else {
    return `${year}-${month}-${day}`;
  }
}

//-----------------------------------------------------------------------------
// Case Manager
//-----------------------------------------------------------------------------

class CaseManager extends material.MdApp {
  oninit() {
    this.bind("#settings", "click", e => this.onsettings(e));
    this.bind("#help", "click", e => this.onhelp(e));
    this.bind("md-menu #backup", "click", e => this.onbackup(e));
    this.bind("md-menu #restore", "click", e => this.onrestore(e));
    this.bind("md-menu #settings", "click", e => this.onsettings(e));
    this.bind("md-menu #help", "click", e => this.onhelp(e));
    this.find("md-search input").focus();
  }

  onupdate() {
    if (!this.state) return;
    this.find("case-list").update(this.state);
    this.find("md-search input").focus();
  }

  async onbackup(e) {
    try {
      // Get file for saving backup.
      let fh = await window.showSaveFilePicker({
        suggestedName: `cases-${date2str(new Date(), false)}.json`,
      });

      // Read data from local case store.
      let backup = await app.backup();
      let data = JSON.stringify(backup);

      // Write backup to file.
      let fw = await fh.createWritable();
      await fw.write(data);
      await fw.close();
    } catch (error) {
      material.inform("Unable to backup data: " + error);
    }
  }

  async onrestore(e) {
    try {
      // Get file for restoring backup.
      let [fh] = await window.showOpenFilePicker({
        types: [{
          description: 'Backup file',
          accept: {'text/json': ['.json']}
        }],
        multiple: false,
      });

      // Read JSON data from file.
      let file = await fh.getFile();
      let data = JSON.parse(await file.text());

      // Ask before overwriting any existing cases.
      let ok = await material.StdDialog.ask("Restore cases",
        `Are you sure you want to restore ${data.casedir.length} cases? ` +
        "This will overwrite any existing data for the restored cases.");
      if (ok) {
        // Restore cases from backup.
        app.restore(data);
      }
    } catch (error) {
      material.inform("Unable to restore data: " + error);
    }
  }

  async onsettings(e) {
    let dialog = new SettingsDialog();
    let ok = await dialog.show();
    if (ok) location.reload();
  }

  onhelp(e) {
    window.open("https://ringgaard.com/knolcase", "_blank");
  }

  prerender() {
    return `
      <md-toolbar>
        <md-toolbar-logo></md-toolbar-logo>
        <div>Case</div>
        <case-search-box id="search"></case-search-box>
        <md-spacer></md-spacer>
        <md-icon-button
          id="help"
          icon="info"
          tooltip="Go to Guide">
        </md-icon-button>
        <md-icon-button
          id="settings"
          icon="settings"
          tooltip="Change settings"
          tooltip-align="right">
        </md-icon-button>
        <md-menu id="menu">
          <md-menu-item id="backup">Backup</md-menu-item>
          <md-menu-item id="restore">Restore</md-menu-item>
          <md-menu-item id="settings">Settings</md-menu-item>
          <md-menu-item id="help">Help</md-menu-item>
        </md-menu>
      </md-toolbar>

      <md-content>
        <case-list></case-list>
      </md-content>
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
    return `
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
    let query = e.detail
    let target = e.target;
    let results = await search(query, [app.search.bind(app), kbsearch]);
    target.populate(query, results);
  }

  onitem(e) {
    let item = e.detail;
    this.find("md-search").clear();
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
    this.find("md-search").clear();
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
      <md-search
        placeholder="Search for case or topic..."
        min-length=2>
      </md-search>
      <md-icon-button
        id="add"
        icon="add"
        tooltip="Open new case">
      </md-icon-button>
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        width: 100%;
        max-width: 800px;
        padding-left: 10px;
        padding-right: 3px;
        align-items: center;
      }
      $ #add {
        padding-left: 5px;
      }
    `;
  }
}

Component.register(CaseSearchBox);

//-----------------------------------------------------------------------------
// Case list
//-----------------------------------------------------------------------------

class CaseList extends material.MdCard {
  oninit() {
    this.bind(null, "click", e => this.onclick(e));
    this.bind(null, "mousedown", e => this.ondown(e));
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
    let collab = row ? row.getAttribute("collab") == "true" : undefined;

    if (button) {
      // Perform action on case.
      let action = button.getAttribute("icon");
      if (action == "delete") {
        let message = `Delete ${link ? "link to " : ""} case #${caseid}?`;
        if (collab) {
          message = `Leave collaboration on case #${caseid}?`;
        }
        material.StdDialog.confirm("Delete case", message, "Delete")
        .then(result => {
          if (result) {
            app.delete_case(caseid, link);
          }
        });
      }
    } else if (caseid) {
      // Open case.
      app.open_case(caseid);
    }
  }

  render() {
    if (!this.state) return null;
    if (this.state.length == 0) {
      return `<getting-started></getting-started>`;
    }
    let h = [];
    h.push("<table>");
    h.push(`
     <thead><tr>
       <th>Case #</th>
       <th></th>
       <th>Name</th>
       <th>Description</th>
       <th>Modified</th>
       <th>Created</th>
       <th></th>
     </tr></thead>
    `);
    h.push("<tbody>");
    for (let rec of this.state) {
      if (rec.nsfw && !settings.nsfw) continue;

      let icon = "";
      if (rec.link) {
        if (rec.secret) {
          icon = '<md-icon icon="key" outlined></md-icon>';
        } else {
          icon = '<md-icon icon="link" outlined></md-icon>';
        }
      } else if (rec.publish) {
        icon = '<md-icon icon="public" outlined></md-icon>';
      } else if (rec.share) {
        if (rec.secret) {
          icon = '<md-icon icon="lock" outlined></md-icon>';
        } else {
          icon = '<md-icon icon="share" outlined></md-icon>';
        }
      }
      if (rec.share) {
        if (rec.shared && rec.shared < rec.modified) {
          icon += "*";
        }
      }
      if (rec.collaborate) {
        icon += '<md-icon icon="people" outlined></md-icon>';
      }

      h.push(`
        <tr case="${rec.id}" link="${rec.link}" collab="${rec.collaborate}">
          <td>${rec.id}</td>
          <td><div>${icon}</div></td>
          <td>${Component.escape(rec.name)}</td>
          <td>${Component.escape(rec.description)}</td>
          <td>${date2str(rec.modified)}</td>
          <td>${date2str(rec.created)}</td>
          <td><md-icon-button icon="delete" tooltip="Delete case">
          </md-icon-button></td>
        </tr>
     `);
    }
    h.push("</tr>");
    return h.join("");
  }

  static stylesheet() {
    return `
      $ {
        overflow: hidden;
      }

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
// Getting started splash screen
//-----------------------------------------------------------------------------

class GettingStarted extends Component {
  render() {
    return `
      <h1>Getting started with KnolCase...</h1>
      <p>Welcome to <b>KnolCase</b>, a case-based knowledge management
      tool for gathering information about topics of interest and organizing
      these into case files.</p>

      <p>Click the <md-icon icon="add"></md-icon> button to open a new
      case. Type the name of the case you want to research and an optional
      description. Then click "Create". This opens the new case and you can
      now start to add topics to the case.</p>
      </p>
      <p>You add new topics to the case by typing the name of the topic in the
      search bar. If the topic is already in the knowledge base, you can just
      select it from the list. This creates a topic that is an extension of an
      existing topic and you can click <md-icon icon="expand_more"></md-icon> to
      see the existing information. Otherwise, pressing Enter without selecting
      a topic will create a new topic.</p>
      <p>You can add information about the topic by clicking the
      <md-icon icon="edit"></md-icon> button in the topic toolbar. Save you
      changes by pressing Ctrl+S or click <md-icon icon="save_alt"></md-icon>
      in the topic toolbar.</p>

      <p>Your data is only stored locally on your own computer (until you
      explicitly choose to share it with others). Click the
      <md-icon icon="save"></md-icon> button in the case toolbar to save your
      changes in your local case database.
      </p>

      <p>For more information, see
      <a href="https://ringgaard.com/knolcase">here</a>.</p>
      ${this.browser_check()}
    `;
  }

  browser_supported() {
    if (typeof HTMLDialogElement !== 'function') return false;
    return true;
  }

  browser_check() {
    if (navigator.userAgent.includes("Version/15.3 Safari")) {
      return "<p><b>KnolCase does NOT work with Safari 15.3. " +
             "Please upgrade to Safari 15.4+ (or use Chrome instead).</b></p>";
    }

    if (!this.browser_supported()) {
      return "<p><b>Your web browser is not supported by KnolCase. " +
             "Please use Google Chrome 98+ for the best experience.</b></p>";
    }

    return "";
  }

  static stylesheet() {
    return `
      $ h1 {
        text-align: center;
      }
      $ p {
        font-size: 1.2rem;
        margin: 1rem 7rem 1rem 7rem;
      }
      $ md-icon {
        font-size: 1.2rem;
        vertical-align: middle;
      }
    `;
  }
}

Component.register(GettingStarted);

//-----------------------------------------------------------------------------
// Settings panel
//-----------------------------------------------------------------------------

class SettingsDialog extends material.MdDialog {
  onopen() {
    this.find("#authorid").value = settings.authorid;
    this.find("#picturesize").value = settings.picturesize;
    this.find("#kbservice").value = settings.kbservice;
    this.find("#collaburl").value = settings.collaburl;
    this.find("#analyzer").value = settings.analyzer;
    this.find("#imagesearch").value = settings.imagesearch;
    this.find("#userscripts").checked = settings.userscripts;
    this.find("#nsfw").checked = settings.nsfw;
  }

  submit() {
    settings.authorid = this.find("#authorid").value;
    settings.picturesize = this.find("#picturesize").value;
    settings.kbservice = this.find("#kbservice").value;
    settings.collaburl = this.find("#collaburl").value;
    settings.analyzer = this.find("#analyzer").value;
    settings.imagesearch = this.find("#imagesearch").value;
    settings.userscripts = this.find("#userscripts").checked;
    settings.nsfw = this.find("#nsfw").checked;
    save_settings();
    this.close(true);
  }

  render() {
    return `
      <md-dialog-top>Settings</md-dialog-top>
      <div id="content">
        <md-text-field
          id="authorid"
          label="Author topic ID">
        </md-text-field>
        <md-text-field
          id="kbservice"
          label="Knowledge service URL">
        </md-text-field>
        <md-text-field
          id="collaburl"
          label="Collaboration service URL">
        </md-text-field>
        <md-text-field
          id="analyzer"
          label="Document analyzer service URL">
        </md-text-field>
        <md-text-field
          id="imagesearch"
          label="Image search URL">
        </md-text-field>
        <md-text-field
          id="picturesize"
          label="Profile picture size">
        </md-text-field>
        <md-switch id="userscripts" label="Enable user scripts"></md-switch>
        <md-switch id="nsfw" label="Show adult content (NSFW)"></md-switch>
      </div>
      </div>
      <md-dialog-bottom>
        <button id="cancel">Cancel</button>
        <button id="submit">Save settings</button>
      </md-dialog-bottom>
    `;
  }

  static stylesheet() {
    return `
      $  {
        max-height: 75vh;
        min-width: 500px;
        overflow: auto;
      }
      $ #content {
        display: flex;
        flex-direction: column;
        row-gap: 16px;
      }
    `;
  }
}

Component.register(SettingsDialog);


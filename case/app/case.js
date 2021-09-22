// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Case-based knowledge management app.

import {Component, OneOf} from "/common/lib/component.js";
import {MdDialog, StdDialog, MdCard} from "/common/lib/material.js";
import {store, settings} from "./global.js";
import {casedb} from "./database.js";

const kbservice = "https://ringgaard.com/kb"

const n_id = store.id;
const n_is = store.is;
const n_isa = store.isa;
const n_name = store.lookup("name");
const n_description = store.lookup("description");
const n_main = store.lookup("main");
const n_caseno = store.lookup("caseno");
const n_created = store.lookup("created");
const n_modified = store.lookup("modified");
const n_topics = store.lookup("topics");
const n_folders = store.lookup("folders");
const n_next = store.lookup("next");
const n_main_subject = store.lookup("P921");
const n_pcase = store.lookup("PCASE");

function date2str(date) {
  return date.toLocaleString();
}

//-----------------------------------------------------------------------------
// Case App
//-----------------------------------------------------------------------------

class CaseApp extends OneOf {
  onconnected() {
    // Open local case database.
    casedb.open().then(() => {
      // Read case directory.
      casedb.readdir().then(caselist => {
        this.caselist = caselist;
        this.refresh();
      });
    });
  }

  add_case(name, description, topic) {
    fetch("/newcase")
    .then(response => store.parse(response))
    .then(casefile => {
      let caseno = casefile.get(n_caseno);

      // Create main topic.
      let main = store.frame([
        n_id, `t/${caseno}/1`,
        n_pcase, `${caseno}`,
      ]);
      if (name) main.add(n_name, name);
      if (description) main.add(n_description, description);
      if (topic) main.add(n_main_subject, store.lookup(topic));

      // Initialize case.
      let ts = new Date().toJSON();
      casefile.add(n_created, ts);
      casefile.add(n_modified, ts);
      casefile.add(n_main, main);
      casefile.add(n_topics, [main]);
      casefile.add(n_folders, store.frame(["Main", [main]]));
      casefile.add(n_next, 2);
      console.log("main", main.text(true));
      console.log("add case", casefile.text(true));

      // Write case to database.
      let rec = casedb.write(casefile);

      // Update case list.
      this.caselist.push(rec);
      this.refresh();
    });
  }

  delete_case(caseid) {
    // Remove case from database.
    casedb.remove(caseid);

    // Update case list.
    this.caselist = this.caselist.filter(rec => rec.id != caseid);
    this.refresh();
  }

  open_case(caseid) {
    casedb.read(caseid).then(casefile => {
      console.log("casefile", casefile.text(true));
      this.update("case-editor", casefile);
    });
  }

  refresh() {
    this.caselist.sort((a, b) => a.modified > b.modified ? -1 : 1);
    manager.update(this.caselist);
  }
}

Component.register(CaseApp);

//-----------------------------------------------------------------------------
// Case Manager
//-----------------------------------------------------------------------------

class CaseManager extends Component {
  onupdate() {
    this.find("case-list").update(this.state);
  }
}

Component.register(CaseManager);

//-----------------------------------------------------------------------------
// Case Editor
//-----------------------------------------------------------------------------

class CaseEditor extends Component {
  onupdated() {
    let casefile = this.state;
    this.find("#caseno").update(casefile.get(n_caseno).toString());

    let code = [];
    code.push(casefile.text(true));
    for (let topic of casefile.get(n_topics)) {
      code.push(topic.text(true));
    }
    this.find("pre").innerHTML = code.join("\n");
  }
}

Component.register(CaseEditor);

//-----------------------------------------------------------------------------
// New case
//-----------------------------------------------------------------------------

class NewCaseDialog extends MdDialog {
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
    return MdDialog.stylesheet() + `
      #content {
        display: flex;
        flex-direction: column;
        row-gap: 16px;
      }
      #name {
        width: 300px;
      }
      #description {
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
    this.bind("md-search", "query", e => this.onquery(e));
    this.bind("md-search", "item", e => this.onitem(e));
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

    this.itemnames = new Map();
    fetch(kbservice + "/query?" + params + "&q=" + encodeURIComponent(query))
    .then(response => response.json())
    .then((data) => {
      let items = [];
      for (let item of data.matches) {
        let elem = document.createElement("md-search-item");
        elem.setAttribute("name", item.text);
        elem.setAttribute("value", item.ref);

        let title = document.createElement("span");
        title.className = "item-title";
        title.appendChild(document.createTextNode(item.text));
        elem.appendChild(title);

        if (item.description) {
          let desciption = document.createElement("span");
          desciption.className = "item-description";
          desciption.appendChild(document.createTextNode(item.description));
          elem.appendChild(desciption);
        }

        items.push(elem);
        this.itemnames[item.ref] = item.text;
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
    let topic = e.detail;
    let name = this.itemnames[topic];
    let dialog = new NewCaseDialog({topic, name});
    dialog.show().then(result => {
      if (result) {
        app.add_case(result.name, result.description, topic);
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

      $ .item-title {
        font-weight: bold;
        display: block;
        padding: 2px 10px 2px 10px;
      }

      $ .item-description {
        display: block;
        padding: 0px 10px 0px 10px;
      }
    `;
  }
}

Component.register(CaseSearchBox);

//-----------------------------------------------------------------------------
// Case list
//-----------------------------------------------------------------------------

class CaseList extends MdCard {
  onupdated() {
    this.bind("table", "click", e => this.onclick(e));
  }

  onclick(e) {
    let row = e.target.closest("tr");
    let button = e.target.closest("md-icon-button");
    let caseid = row ? parseInt(row.getAttribute("case")) : undefined;
    if (!caseid) return;

    if (button) {
      let action = button.getAttribute("icon");
      let message = `Delete case #${caseid}?`;
      StdDialog.confirm("Delete case", message, "Delete").then(result => {
        if (result) {
          app.delete_case(caseid);
        }
      });
    } else {
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
      h.push(`
        <tr case="${rec.id}">
          <td>${rec.id}</td>
          <td>${Component.escape(rec.name)}</td>
          <td>${Component.escape(rec.description)}</td>
          <td>${date2str(rec.created)}</td>
          <td>${date2str(rec.modified)}</td>
          <td>
            <md-icon-button icon="delete" outlined>
            </md-icon-button>
          </td>
        </tr>
     `);
    }
    h.push("</tr>");
    return h.join("");
  }

  static stylesheet() {
    return MdCard.stylesheet() + `
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

      $ td:nth-child(3) { /* description */
        width: 100%;
      }
    `;
  }
}

Component.register(CaseList);

//-----------------------------------------------------------------------------
// HTML template
//-----------------------------------------------------------------------------

document.body.innerHTML = `
<case-app id="app" selected="case-manager">
  <case-manager id="manager">
    <md-column-layout>
      <md-toolbar>
        <md-toolbar-logo></md-toolbar-logo>
        <div>Cases</div>
        <case-search-box id="search"></kb-search-box>
      </md-toolbar>

      <md-content>
        <case-list></case-list>
      </md-content>
    </md-column-layout>
  </case-manager>
  <case-editor id="editor">
    <md-column-layout>
      <md-toolbar>
        <md-toolbar-logo></md-toolbar-logo>
        <div>Case #<md-text id="caseno"></md-text></div>
        <case-search-box id="search"></kb-search-box>
      </md-toolbar>

      <md-content>
        <md-card>
          <pre id="code"></pre>
        </md-card>
      </md-content>
    </md-column-layout>
  </case-editor>
</case-app>
`;

var app = document.getElementById("app");
var manager = document.getElementById("manager");
var editor = document.getElementById("editor");


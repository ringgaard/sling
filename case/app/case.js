// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Case-based knowledge management app.

import {Component} from "/common/lib/component.js";
import {MdDialog, StdDialog, MdCard} from "/common/lib/material.js";
import {Store, Encoder} from "/common/lib/frame.js";

const kbservice = "https://ringgaard.com/kb"

let store = new Store();
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
// Database
//-----------------------------------------------------------------------------

class Deferred {
  constructor() {
    this.promise = new Promise((resolve, reject) => {
      this.resolve = resolve;
      this.reject = reject;
    });
  }
}

class CaseDatabase {
  // Open database.
  open() {
    // Open database.
    let deferred = new Deferred();
    let request = window.indexedDB.open("Case", 1);
    request.onerror = e => {
      deferred.reject(this);
    };

    // Create database if needed.
    request.onupgradeneeded = e => {
      console.log("Database upgrade");
      let db = event.target.result;

      // Create case directory.
      let casedir = db.createObjectStore("casedir", { keyPath: "id" });
      casedir.transaction.oncomplete = e => {
        console.log("Case directory created");
      };

      // Create case data store.
      let casedata = db.createObjectStore("casedata", { keyPath: "id" });
      casedata.transaction.oncomplete = e => {
        console.log("Case data store created");
      };
    }

    // Store database connection and install global error handler on success.
    request.onsuccess = e => {
      this.db = event.target.result;
      this.db.onerror = e => this.onerror(e);
      deferred.resolve(this);
    };

    return deferred.promise;
  }

  onerror(e) {
    console.log("Database error", e.target.error);
  }

  write(casefile) {
    // Build case directory record.
    let caseno = casefile.get(n_caseno);
    let main = casefile.get(n_main);
    let rec = {
      id: caseno,
      name: main.get(n_name),
      description: main.get(n_description),
      created: new Date(casefile.get(n_created)),
      modified: new Date(casefile.get(n_modified)),
    };

    // Write record to database.
    let tx = this.db.transaction(["casedir", "casedata"], "readwrite");
    let casedir = tx.objectStore("casedir");
    let dirrequest = casedir.add(rec);
    dirrequest.onsuccess = e => {
      console.log("Added record", e.target.result, "to case directory");
    }

    // Encode case data.
    let encoder = new Encoder(store);
    for (let topic of casefile.get(n_topics)) {
      encoder.encode(topic);
    }
    encoder.encode(casefile);

    // Write case data.
    let casedata = tx.objectStore("casedata");
    let datarequest = casedata.add({id: caseno, data: encoder.output()});
    datarequest.onsuccess = e => {
      console.log("Added record", e.target.result, "to case store");
    }
    datarequest.onerror = e => {
      console.log("Error writing to case store", e.target.result);
    }

    return rec;
  }

  remove(caseid) {
    // Remove case from directory.
    let tx = this.db.transaction(["casedir", "casedata"], "readwrite");
    let casedir = tx.objectStore("casedir");
    let dirrequest = casedir.delete(caseid);
    dirrequest.onsuccess = e => {
      console.log("Removed record", caseid, "from case directory");
    }

    // Remove case from data store.
    let casedata = tx.objectStore("casedata");
    let datarequest = casedata.delete(caseid);
    datarequest.onsuccess = e => {
      console.log("Removed record", caseid, "from case store");
    }
    datarequest.onerror = e => {
      console.log("Error removing data from case store", e.target.result);
    }
  }

  readdir() {
    // Read case directory and add to list.
    let deferred = new Deferred();
    let caselist = new Array();
    let casedir = this.db.transaction("casedir").objectStore("casedir");
    casedir.openCursor().onsuccess = e => {
      var cursor = e.target.result;
      if (cursor) {
        caselist.push(cursor.value);
        cursor.continue();
      } else {
        deferred.resolve(caselist);
      }
    };
    return deferred.promise;
  }
};

var casedb = new CaseDatabase();

//-----------------------------------------------------------------------------
// App
//-----------------------------------------------------------------------------

class CaseApp extends Component {
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

  refresh() {
    this.caselist.sort((a, b) => a.modified > b.modified ? -1 : 1);
    this.find("#cases").update(this.caselist);
  }
}

Component.register(CaseApp);

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
        this.match("#app").add_case(result.name, result.description, topic);
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
          this.match("#app").delete_case(caseid);
        }
      });
    } else {
      StdDialog.alert("Open case", `Open case #${caseid}`);
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
<case-app id="app">
  <md-column-layout class="desktop">
    <md-toolbar>
      <md-toolbar-logo></md-toolbar-logo>
      <div>Cases</div>
      <case-search-box id="search"></kb-search-box>
    </md-toolbar>

    <md-content>
      <case-list id="cases"></case-list>
    </md-content>
  </md-column-layout>
</kb-app>
`;


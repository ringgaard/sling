// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Case-based knowledge management app.

import {Component} from "/common/lib/component.js";
import {MdDialog, StdDialog} from "/common/lib/material.js";
import {Store} from "/common/lib/frame.js";

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
const n_topics = store.lookup("topics");
const n_folders = store.lookup("folders");
const n_next = store.lookup("next");
const n_modified = store.lookup("modified");
const n_main_subject = store.lookup("P921");
const n_pcase = store.lookup("PCASE");

//-----------------------------------------------------------------------------
// App
//-----------------------------------------------------------------------------

class CaseApp extends Component {
  onconnected() {
  }

  addcase(name, description, topic) {
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
    });
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
      <md-dialog-top>Create new case</md-dialog-top>
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
        <button id="submit">Create case</button>
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
        this.match("#app").addcase(result.name, result.description, topic);
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
// HTML template
//-----------------------------------------------------------------------------

document.body.innerHTML = `
<case-app id="app">
  <md-column-layout class="desktop">
    <md-toolbar>
      <md-toolbar-logo></md-toolbar-logo>
      <div>Case Manager</div>
      <case-search-box id="search"></kb-search-box>
    </md-toolbar>

    <md-content>
    </md-content>
  </md-column-layout>
</kb-app>
`;


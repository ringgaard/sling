// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Case-based knowledge management app.

import {Component, OneOf} from "/common/lib/component.js";
import {store, settings} from "./global.js";
import {casedb} from "./database.js";

import "./manager.js";
import "./case.js";

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

  show_manager() {
    this.update("case-manager", this.caselist);
  }

  refresh() {
    this.caselist.sort((a, b) => a.modified > b.modified ? -1 : 1);
    manager.update(this.caselist);
  }
}

Component.register(CaseApp);

//-----------------------------------------------------------------------------
// HTML template
//-----------------------------------------------------------------------------

document.body.innerHTML = `
<case-app id="app" selected="case-manager">
  <case-manager id="manager">
    <md-column-layout>
      <md-toolbar>
        <md-icon-button icon="menu"></md-icon-button>
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
        <md-icon-button id="back" icon="menu"></md-icon-button>
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


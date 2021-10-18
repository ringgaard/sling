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
const n_case_file = store.lookup("Q108673968");
const n_main_subject = store.lookup("P921");
const n_instance_of = store.lookup("P31");
const n_sling_case_no = store.lookup("PCASE");

//-----------------------------------------------------------------------------
// Case App
//-----------------------------------------------------------------------------

class CaseApp extends OneOf {
  async onconnected() {
    // Handle navigation events.
    window.onpopstate = e => this.onpopstate(e);

    // Open local case database.
    await casedb.open();

    // Parse url.
    let m = window.location.pathname.match(/\/c\/(\d+)/);
    if (m) {
      // Open case specified in url.
      let caseid = parseInt(m[1]);
      this.open_case(caseid);
    } else {
      // Show case list.
      this.show_manager();
    }
  }

  onpopstate(e) {
    let caseid = e.state;
    if (caseid && caseid != "*") {
      this.open_case(caseid);
    } else {
      this.find("#editor").close();
    }
  }

  async add_case(name, description, topicid) {
    let response = await fetch("/newcase");
    let casefile = await store.parse(response);
    let caseno = casefile.get(n_caseno);
    let next = 1;
    let topics = new Array();
    let main_topics = new Array();

    // Create main topic for case file.
    let main = store.frame(`t/${caseno}/${next++}`);
    topics.push(main);
    main_topics.push(main);
    main.add(n_instance_of, n_case_file);
    if (name) main.add(n_name, name);
    if (description) main.add(n_description, description);
    main.add(n_sling_case_no, caseno.toString());

    // Add initial topic.
    if (topicid) {
      let topic = store.frame(`t/${caseno}/${next++}`);
      topics.push(topic);
      main_topics.push(topic);
      topic.add(n_is, store.lookup(topicid));
      if (name) topic.add(n_name, name);
    }

    // Initialize case.
    let ts = new Date().toJSON();
    casefile.add(n_created, ts);
    casefile.add(n_modified, ts);
    casefile.add(n_main, main);
    casefile.add(n_topics, topics);
    casefile.add(n_folders, store.frame(["Main", main_topics]));
    casefile.add(n_next, next);

    // Write case to database.
    let rec = casedb.write(casefile, true);

    // Update case list.
    this.caselist.push(rec);
    this.refresh_manager();

    // Show new case.
    return this.open_case(caseno)
  }

  delete_case(caseid) {
    // Remove case from database.
    casedb.remove(caseid);

    // Update case list.
    this.caselist = this.caselist.filter(r => r.id != caseid);
    this.refresh_manager();
  }

  async open_case(caseid) {
    let casefile = await casedb.read(caseid);
    let main = casefile.get(n_main);
    let name = main.get(n_name);
    window.document.title = `Case #${caseid}: ${name}`;
    this.update("case-editor", casefile);
    history.pushState(caseid, "", "/c/" + caseid);
    return casefile;
  }

  save_case(casefile) {
    // Update modification time.
    let ts = new Date().toJSON();
    casefile.set(n_modified, ts);

    // Write case to database.
    let rec = casedb.write(casefile, false);

    // Update case list.
    if (this.caselist) {
      this.caselist = this.caselist.filter(r => r.id != rec.id);
      this.caselist.push(rec);
    }
  }

  show_manager() {
    history.pushState("*", "", "/c/");
    window.document.title = `SLING Cases`;

    if (this.caselist) {
      this.refresh_manager();
    } else {
      // Read case directory.
      casedb.readdir().then(caselist => {
        this.caselist = caselist;
        this.refresh_manager();
      });
    }
  }

  refresh_manager() {
    this.caselist.sort((a, b) => a.modified > b.modified ? -1 : 1);
    this.update("#manager", this.caselist);
  }
}

Component.register(CaseApp);

//-----------------------------------------------------------------------------
// HTML template
//-----------------------------------------------------------------------------

document.body.innerHTML = `
<case-app id="app" selected="case-manager">
  <case-manager id="manager"></case-manager>
  <case-editor id="editor"></case-editor>
</case-app>
`;


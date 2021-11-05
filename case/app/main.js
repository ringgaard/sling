// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Case-based knowledge management app.

import {Component, OneOf} from "/common/lib/component.js";
import {StdDialog} from "/common/lib/material.js";
import {store, settings} from "./global.js";
import {casedb} from "./database.js";

import "./manager.js";
import "./case.js";

const n_id = store.id;
const n_is = store.is;
const n_isa = store.isa;
const n_name = store.lookup("name");
const n_description = store.lookup("description");
const n_caseno = store.lookup("caseno"); // TODO remove
const n_caseid = store.lookup("caseid");
const n_main = store.lookup("main");
const n_created = store.lookup("created");
const n_modified = store.lookup("modified");
const n_topics = store.lookup("topics");
const n_folders = store.lookup("folders");
const n_next = store.lookup("next");
const n_link = store.lookup("link");
const n_case_file = store.lookup("Q108673968");
const n_main_subject = store.lookup("P921");
const n_instance_of = store.lookup("P31");
const n_case = store.lookup("PCASE");

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

      // Read case list in background.
      casedb.readdir().then(caselist => {
        this.caselist = caselist;
      });
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

  async read_case(caseid) {
    // Try to read case from local database.
    let casefile = await casedb.read(caseid);
    if (!casefile) {
      // Try to get case from server.
      let response = await fetch(`/case/fetch?id=${caseid}`);
      if (!response.ok) return null;
      casefile = await store.parse(response);
      casefile.add(n_link, true);

      // Add linked case to directory.
      let rec = casedb.writemeta(casefile);

      // Update case list.
      if (this.caselist) {
        this.caselist = this.caselist.filter(r => r.id != caseid);
        this.caselist.push(rec);
        this.refresh_manager();
      }
    }
    return casefile;
  }

  async add_case(name, description, topicid) {
    let response = await fetch("/case/new");
    let newcase = await store.parse(response);
    let caseid = newcase.get(n_caseid);
    if (!caseid) caseid = newcase.get(n_caseno); // TODO: remove
    let next = 1;
    let topics = new Array();
    let main_topics = new Array();

    // Create main topic for case file.
    let casefile = store.frame();
    let main = store.frame(`c/${caseid}`);
    topics.push(main);
    main_topics.push(main);
    main.add(n_instance_of, n_case_file);
    if (name) main.add(n_name, name);
    if (description) main.add(n_description, description);
    main.add(n_case, caseid.toString());

    // Add initial topic.
    if (topicid) {
      let topic = store.frame(`t/${caseid}/${next++}`);
      topics.push(topic);
      main_topics.push(topic);
      topic.add(n_is, store.lookup(topicid));
      if (name) topic.add(n_name, name);
      main.add(n_main_subject, topic);
    }

    // Initialize case.
    let ts = new Date().toJSON();
    casefile.add(n_caseid, caseid);
    casefile.add(n_created, ts);
    casefile.add(n_modified, ts);
    casefile.add(n_topics, topics);
    casefile.add(n_main, main);
    casefile.add(n_folders, store.frame(["Main", main_topics]));
    casefile.add(n_next, next);

    console.log("newcase", casefile.text());

    // Switch to case editor with new case.
    window.document.title = `Case #${caseid}: ${name}`;
    this.update("case-editor", casefile);
    this.find("case-editor").mark_dirty();
    history.pushState(caseid, "", "/c/" + caseid);
    return casefile;
  }

  delete_case(caseid, link) {
    // Remove case from database.
    casedb.remove(caseid);

    // Update case list.
    this.caselist = this.caselist.filter(r => r.id != caseid);
    this.refresh_manager();
  }

  async open_case(caseid) {
    // Try to read case from local database.
    let casefile = await this.read_case(caseid);
    if (!casefile) {
      StdDialog.error(`Case #${caseid} not found`);
      return;
    }

    // Upgrade legacy cases.
    // TODO remove after legacy upgrade.
    if (casefile.get("caseno")) {
      console.log("Upgrading legacy case file:", casefile.text(true));
      store.unregister(casefile);
      casefile.rename(n_caseno, n_caseid);
      let main = casefile.get(n_main);
      main.slots[1] = "c/" + caseid;
      store.frames.set("c/" + caseid, main);
      console.log("Upgraded:", casefile.text(true));
    }

    // Switch to case editor with new case.
    let main = casefile.get(n_main);
    let name = main.get(n_name);
    window.document.title = `Case #${caseid}: ${name}`;
    this.update("case-editor", casefile);
    history.pushState(caseid, "", "/c/" + caseid);
    return casefile;
  }

  save_case(casefile) {
    // Write case to database.
    let rec = casedb.write(casefile);

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

  search(query, full) {
    query = query.toLowerCase();
    let results = [];
    let partial = [];
    if (this.caselist) {
      for (let caserec of this.caselist) {
        let match = false;
        let submatch = false;
        if (caserec.id == query) {
            results.push(caserec);
        } else {
          let normalized = caserec.name.toLowerCase();
          if (normalized == query) {
            results.push(caserec);
          } else if (!full && normalized.startsWith(query)) {
            partial.push(caserec);
          }
        }
      }
    }
    results.push(...partial);
    return results;
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


// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Case-based knowledge management app.

import {Component, OneOf} from "/common/lib/component.js";
import {StdDialog, inform} from "/common/lib/material.js";
import {store, settings} from "./global.js";
import {casedb} from "./database.js";
import {decrypt} from "./crypto.js";
import {oauth_callback} from "./wikibase.js";

import "./manager.js";
import "./case.js";

const n_id = store.id;
const n_is = store.is;
const n_isa = store.isa;
const n_name = store.lookup("name");
const n_description = store.lookup("description");
const n_caseid = store.lookup("caseid");
const n_main = store.lookup("main");
const n_created = store.lookup("created");
const n_modified = store.lookup("modified");
const n_topics = store.lookup("topics");
const n_folders = store.lookup("folders");
const n_next = store.lookup("next");
const n_link = store.lookup("link");
const n_encryption = store.lookup("encryption");
const n_case_file = store.lookup("Q108673968");
const n_author = store.lookup("P50");
const n_main_subject = store.lookup("P921");
const n_instance_of = store.lookup("P31");
const n_case = store.lookup("PCASE");

// Parse parameters in URL fragment.
function parse_url_fragment() {
  let params = new Map();
  let fragment = window.location.hash;
  if (fragment.startsWith('#')) {
    for (let kv of fragment.slice(1).split(',')) {
      let delim = kv.indexOf('=');
      if (delim == -1) {
        params.set(kv.trim(), null);
      } else {
        params.set(kv.slice(0, delim).trim(), kv.slice(delim + 1).trim());
      }
    }
  }
  return params;
}

//-----------------------------------------------------------------------------
// Case App
//-----------------------------------------------------------------------------

class CaseApp extends Component {
  async onconnected() {
    // Get case manager and editor.
    this.manager = this.find("case-manager");
    this.editor = this.find("case-editor");

    // Handle navigation events.
    window.onpopstate = e => this.onpopstate(e);

    // Open local case database.
    await casedb.open();

    // Check for OAuth callback.
    if (window.location.search.includes("oauth_")) {
      if (await oauth_callback()) {
        inform("You have now been authorized to publish to Wikidata from SLING");
      } else {
        inform("Failed to authorized you for publishing to Wikidata");
      }
    }

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
      this.editor.close();
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

      // Decrypt case if needed.
      if (casefile.has(n_encryption)) {
        // Get secret key from url fragment.
        let fragment = parse_url_fragment();
        let secret = fragment.get("k");

        // Try to get secret from case directory if there is no key in the url.
        if (!secret) {
          if (!this.caselist) this.caselist = await casedb.readdir();
          for (let caserec of this.caselist) {
            if (caserec.id == caseid) {
              secret = caserec.secret;
              break;
            }
          }
        }

        // Decrypt case using shared secret key.
        if (!secret) return null;
        casefile = await decrypt(casefile, secret);
      }

      // Add linked case to directory.
      casefile.add(n_link, true);
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

  async add_case(name, description, topicid, topictype) {
    let response = await fetch("/case/new");
    let newcase = await store.parse(response);
    let caseid = newcase.get(n_caseid);
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
    if (settings.authorid) main.add(n_author, store.lookup(settings.authorid));
    main.add(n_case, caseid.toString());

    // Add initial topic.
    if (topicid) {
      let topic = store.frame(`t/${caseid}/${next++}`);
      topics.push(topic);
      main_topics.push(topic);
      topic.add(n_is, store.lookup(topicid));
      if (topictype) topic.add(n_instance_of, topictype);
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

    // Switch to case editor with new case.
    await this.show_case(casefile);
    this.editor.mark_dirty();

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
    try {
      // Try to read case from local database.
      let casefile = await this.read_case(caseid);
      if (!casefile) {
        inform(`Case #${caseid} not found`);
        return;
      }

      // Switch to case editor with new case.
      await this.show_case(casefile);

      return casefile;
    } catch (e) {
      inform(`Error opening case #${caseid}: ${e}`);
    }
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

  async show_case(casefile) {
    let caseid = casefile.get(n_caseid);
    let main = casefile.get(n_main);
    let name = main.get(n_name);

    history.pushState(caseid, "", "/c/" + caseid);
    window.document.title = `#${caseid} ${name}`;

    this.manager.style.display = "none";
    this.editor.style.display = "";

    await this.manager.update(null);
    await this.editor.update(casefile);
  }

  async show_manager() {
    history.pushState("*", "", "/c/");
    window.document.title = "SLING Case";

    this.editor.style.display = "none";
    this.manager.style.display = "";

    await this.editor.update(null);
    if (this.caselist) {
      await this.refresh_manager();
    } else {
      // Read case directory.
      this.caselist = await casedb.readdir();
      await this.refresh_manager();
    }
  }

  async refresh_manager() {
    this.caselist.sort((a, b) => a.modified > b.modified ? -1 : 1);
    await this.manager.update(this.caselist);
  }

  search(query, full) {
    query = query.toLowerCase();
    let results = [];
    let partial = [];
    if (this.caselist) {
      for (let caserec of this.caselist) {
        let match = false;
        let submatch = false;
        if (query == caserec.id || query == `c/${caserec.id}`) {
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


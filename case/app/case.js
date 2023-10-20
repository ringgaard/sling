// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {MdApp, MdDialog, StdDialog, MdIcon, MdSearchResult, inform}
       from "/common/lib/material.js";
import {Store, Frame, Encoder, Printer, Reader} from "/common/lib/frame.js";
import {store, frame, settings} from "/common/lib/global.js";
import {LabelCollector, ItemCollector} from "/common/lib/datatype.js";

import * as plugins from "./plugins.js";
import * as importers from "./importers.js";
import {Drive} from "./drive.js";
import {wikidata_initiate, wikidata_export} from "./wikibase.js";
import {generate_key, encrypt} from "./crypto.js";
import {Collaboration} from "./collab.js";
import {SearchIndex, search, kbsearch, SearchResultsDialog} from "./search.js";
import {get_property_index} from "./schema.js";
import "./drawer.js";
import "./sidebar.js";
import "./topic.js";

const n_id = store.id;
const n_is = store.is;
const n_isa = store.isa;
const n_name = frame("name");
const n_alias = frame("alias");
const n_description = frame("description");
const n_caseid = frame("caseid");
const n_main = frame("main");
const n_topics = frame("topics");
const n_folders = frame("folders");
const n_next = frame("next");
const n_publish = frame("publish");
const n_share = frame("share");
const n_collaborate = frame("collaborate");
const n_secret = frame("secret");
const n_link = frame("link");
const n_collab = frame("collab");
const n_userid = frame("userid");
const n_credentials = frame("credentials");
const n_created = frame("created");
const n_modified = frame("modified");
const n_shared = frame("shared");
const n_media = frame("media");
const n_case_file = frame("Q108673968");
const n_instance_of = frame("P31");
const n_author = frame("P50");
const n_participant = frame("P710");
const n_has_quality = frame("P1552");
const n_nsfw = frame("Q2716583");

const n_document = frame("Q49848");
const n_image = frame("Q478798");
const n_publication_date = frame("P577");
const n_retrieved = frame("P813");
const n_url = frame("P2699");
const n_data_size = frame("P3575");
const n_media_type = frame("P1163");
const n_copyright = frame("P6216");
const n_copyrighted = frame("Q50423863");

const media_file_types = [
  "image/gif",
  "image/jpeg",
  "image/png",
  "image/webp",
];

const work_ownership = new Set();

// Date as SLING numeric date.
function date_number(d) {
  let year = d.getFullYear();
  let month = d.getMonth() + 1;
  let day = d.getDate();
  return year * 10000 + month * 100 + day;
}

// Write topics to clipboard.
async function write_to_clipboard(topics) {
  // Convert selected topics to text.
  let printer = new Printer(store);
  for (let topic of topics) {
    printer.print(topic);
    printer.write("\n");

  }

  // Write selected topics to clipboard.
  if (!navigator.clipboard) throw "Access to clipboard denied";
  await navigator.clipboard.writeText(printer.output);
}

// Read topics from clipboard.
async function read_from_clipboard() {
  // Read clipboard text.
  if (!navigator.clipboard) throw "Access to clipboard denied";
  let clipboard = await navigator.clipboard.readText();

  // Try to parse data SLING frames if it starts with '{' (and it's not JSON).
  if (clipboard.charAt(0) == "{" && !clipboard.match(/^\{\s*\"/)) {
    try {
      let store = new Store();
      let reader = new Reader(store, clipboard);
      let frames = new Array();
      while (!reader.done()) {
        let obj = reader.parse();
        if (obj instanceof Frame) frames.push(obj);
      }
      return frames;
    } catch (error) {
      console.log("ignore sling parse error", error);
    }
  }
}

class CaseEditor extends MdApp {
  oninit() {
    this.attach(this.onitem, "item", "omni-box md-search");
    this.attach(this.onenter, "enter", "omni-box md-search");

    this.attach(this.ondrawer, "click", "#drawer");
    this.attach(this.onmerge, "click", "#merge");
    if (settings.userscripts) {
      this.attach(this.onscript, "click", "#script");
    }
    this.attach(this.onwikiexport, "click", "#wikiexport");
    this.attach(this.onaddlink, "click", "#addlink");
    this.attach(this.onsave, "click", "#save");
    this.attach(this.oninvite, "click", "#invite");
    this.attach(this.onshare, "click", "#share");
    this.attach(this.close, "click", "#home");

    this.attach(this.onsave, "click", "md-menu #save");
    this.attach(this.onfilesave, "click", "md-menu #filesave");
    this.attach(this.onshare, "click", "md-menu #share");
    this.attach(this.oncollaborate, "click", "md-menu #collaborate");
    this.attach(this.onimgcache, "click", "md-menu #imgcache");
    this.attach(this.onimport, "click", "md-menu #import");
    this.attach(this.onwikiexport, "click", "md-menu #wikiexport");
    this.attach(this.onupload, "click", "md-menu #upload");
    this.attach(this.oncopyall, "click", "md-menu #copyall");
    this.attach(this.onstats, "click", "md-menu #stats");

    this.attach(this.oncut, "cut");
    this.attach(this.oncopy, "copy");
    this.attach(this.onpaste, "paste");

    this.attach(this.onnavigate, "navigate");
    this.attach(this.onannotate, "annotate");
    this.attach(this.onreconcile, "reconcile");

    document.addEventListener("keydown", e => this.onkeydown(e));
    window.addEventListener("beforeunload", e => this.onbeforeunload(e));

    this.sidebar = this.find("#sidebar");
    let app = this.match("#app");

    let omnibox = this.find("omni-box");
    omnibox.add(this.search.bind(this));
    omnibox.add(kbsearch);
    omnibox.add(app.search.bind(app));
    omnibox.add((query, results, options) => {
      if (!options.keyword && query.length < 80) {
        results.push({
          title: "more...",
          description: 'search for "' + query + '" 🔎',
          query: query,
          onitem: item => { omnibox.set(item.query + "?"); },
        });
      }
    });
  }

  onbeforeunload(e) {
    // Notify about unsaved changes.
    if (this.dirty) {
      e.preventDefault();
      e.returnValue = "";
    }
  }

  get_index() {
    if (!this.index) this.index = new SearchIndex(this.topics);
    return this.index;
  }

  async onnavigate(e) {
    e.preventDefault();
    e.stopPropagation();
    let ref = e.detail.ref;
    let position = e.detail.position;

    let index = this.get_index();
    let topic = index.ids.get(ref);
    if (topic) {
      // Navigate to existing topic.
      this.navigate_to(topic);
    } else if (e.detail.event.ctrlKey) {
      // Add new linked topic and navigate to it.
      let item = frame(ref);
      let link = await this.add_topic_link(item, position);
      this.redirect(item, link);
      this.navigate_to(link);
    } else {
      // Open external knowledge browser for item.
      window.open(`${settings.kbservice}/kb/${ref}`, "_blank");
    }
  }

  async onannotate(e) {
    if (this.readonly) return;
    let mention = e.detail.mention;
    let annotation = mention.annotation;
    let link = store.resolve(annotation);
    let source = mention.document.source;

    // Create new topic unless there is already a topic for the item.
    let topic = link && this.get_index().ids.get(link.id);
    if (!topic) {
      topic = await this.new_topic(null);
      topic.put(n_name, mention.text(true));
      if (link && link.id) topic.put(n_is, link);
      if (annotation && !annotation.id) {
        annotation.set(n_is, topic);
      } else {
        mention.annotation = topic;
      }
    }

    // Add annotations from document.
    if (annotation && !annotation.id) {
      let propidx = await get_property_index();
      for (let [name, value] of annotation) {
        if (name == n_is || name == n_isa) continue;
        let target = store.resolve(value);
        let qualifiers = store.frame();
        if ((value instanceof Frame) && value.isanonymous()) {
          for (let [qname, qvalue] of value) {
            if (qname == n_is || qname == n_isa) continue;
            if (!propidx.allowed_qualifier(name, qname)) continue;
            qualifiers.put(qname, store.resolve(qvalue));
          }
        }
        if (qualifiers.length == 0) {
          topic.put(name, target);
        } else {
          qualifiers.insert(0, n_is, target);
          topic.put(name, qualifiers);
        }
      }
    }

    // Add phrase mapping.
    if (source instanceof Frame) {
      source.set(mention.text(), topic);
    }

    this.update_topic(topic);
    this.topic_updated(topic);
    await this.update_topics();
    this.navigate_to(topic);
  }

  async onreconcile(e) {
    let mention = e.detail.mention;
    let query = mention.text(true);

    function docsearch(query, results, options) {
      for (let m of mention.document.search(query, options.submatch)) {
        let match = store.resolve(m.annotation);
        if (match && match.id) {
          let name = match.get(n_name) || m.text(true) || "???";
          results.push({
            ref: match.id,
            name: name,
            title: name + " 📖",
            description: match.get(n_description),
            topic: match,
          });
        }
      }
    }

    let backends = [this.search.bind(this), docsearch, kbsearch];
    let options = {
      full: true,
      swap: true,
      plural: true,
      submatch: true,
      local: this.get_index(),
    };

    this.style.cursor = "wait";
    let results = await search(query, backends, options);
    this.style.cursor = "";

    let dialog = new SearchResultsDialog({
      title: "Reconcile with...",
      items: results});
    let ref = await dialog.show();
    let dirty = false;
    if (ref === null) {
      if (mention.annotation) {
        mention.annotation.remove(n_is);
      } else {
        mention.annotation = null;
      }

      let source = mention.document.source;
      if (source instanceof Frame) {
        let text = mention.text();
        source.purge((name, value) => name == text);
      }
      dirty = true;
    } else if (ref) {
      if (mention.annotation) {
        mention.annotation.set(n_is, frame(ref));
      } else {
        mention.annotation = frame(ref);
      }

      let source = mention.document.source;
      if (source instanceof Frame) {
        source.set(mention.text(), frame(ref));
      }
      dirty = true;
    }

    let context = mention.document.context;
    if (dirty && context && context.topic) {
      this.topic_updated(context.topic);
      this.mark_dirty();
    }
  }

  editing() {
    return this.find("topic-list").editing();
  }

  onkeydown(e) {
    if ((e.ctrlKey || e.metaKey) && e.code === "KeyS") {
      e.preventDefault();
      this.onsave(e);
    } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyM") {
      this.onmerge(e);
    } else if (e.code === "Escape") {
      this.find("#search").clear();
    } else if (e.code === "PageDown" && !e.ctrlKey && !this.editing()) {
      if (e.altKey) {
        this.move_folder_down(this.folder);
      } else if (e.shiftKey) {
        this.next_folder();
      }
    } else if (e.code === "PageUp" && !e.ctrlKey && !this.editing()) {
      if (e.altKey) {
        this.move_folder_up(this.folder);
      } else if (e.shiftKey) {
        this.prev_folder();
      }
    }
  }

  async search(query, results, options = {}) {
    // Search topcis in case file.
    let index = this.get_index();
    for (let hit of index.hits(query, options)) {
      let topic = hit.topic;
      let name = topic.get(n_name);
      results.push({
        ref: topic.id,
        name: name,
        title: name + (topic == this.main ? " 🗄️" : " ⭐"),
        description: topic.get(n_description),
        topic: topic,
      });
    }

    // Search topics in linked case files.
    for (let link of this.links) {
      let index = this.linksindex.get(link);
      if (!index) {
        index = new SearchIndex(link.get(n_topics));
        this.linksindex.set(link, index);
      }
      for (let hit of index.hits(query, options)) {
        let topic = hit.topic;
        let name = topic.get(n_name);
        results.push({
          ref: topic.id,
          name: name,
          title: name + " 🔗",
          description: topic.get(n_description),
          topic: topic,
          casefile: link,
        });
      }
    }

    // Search plug-ins.
    let context = new plugins.Context(null, this.casefile, this);
    context.options = options;
    await plugins.search_plugins(context, query, results);
  }

  onenter(e) {
    let name = e.detail.trim();
    if (name) {
      this.add_new_topic(null, name);
    }
  }

  async onitem(e) {
    let item = e.detail;
    if (item.onitem) {
      this.style.cursor = "wait";
      try {
        await item.onitem(item);
      } catch(e) {
        inform("Plug-in error: " + e);
      }
      this.style.cursor = "";
      if (item.context) await item.context.refresh();
    } else if (item.topic) {
      if (item.casefile) {
        let link = await this.add_topic_link(item.topic);
        this.navigate_to(link);
      } else {
        this.navigate_to(item.topic);
      }
    } else if (item.caserec) {
      this.add_case_link(item.caserec);
    } else {
      let topic = this.get_index().ids.get(item.ref);
      if (topic) {
        this.navigate_to(topic);
      } else {
        this.add_new_topic(item.ref, item.name);
      }
    }
  }

  ondrawer(e) {
    this.find("drawer-panel").toogle();
  }

  async onsave(e) {
    if (this.readonly) return;
    if (this.dirty) {
      // Update modification time.
      let ts = new Date().toJSON();
      this.casefile.set(n_modified, ts);

      // Delete scraps.
      this.purge_scraps();

      // Save case to local database.
      await this.match("#app").save_case(this.casefile);
      this.mark_clean();
    }
  }

  async onshare(e) {
    if (this.readonly) return;
    let casefile = this.localcase || this.casefile;
    let share = casefile.get(n_share);
    let publish = casefile.get(n_publish);
    let secret = casefile.get(n_secret);
    let dialog = new SharingDialog({share, publish, secret});
    let result = await dialog.show();
    if (result) {
      // Update sharing information.
      casefile.set(n_share, result.share);
      casefile.set(n_publish, result.publish);
      casefile.set(n_secret, result.secret);
      if (this.collab) {
        this.casefile.set(n_share, result.share);
        this.casefile.set(n_publish, result.publish);
        this.casefile.set(n_secret, result.secret);
      }

      // Update modification and sharing time.
      let ts = new Date().toJSON();
      if (this.collab) ts = await this.collab.flush();
      casefile.set(n_modified, ts);
      if (result.share) {
        casefile.set(n_shared, ts);
      } else {
        casefile.set(n_shared, null);
      }

      // Save case before sharing.
      this.purge_scraps();
      this.mark_clean();
      await this.match("#app").save_case(casefile);

      // Encode case file.
      var data;
      if (result.secret) {
        // Share encrypted case.
        let encrypted = await encrypt(this.casefile);
        data = encrypted.encode();
      } else if (result.share) {
        // Encode case for sharing.
        data = this.encoded();
      } else {
        // Do not send case content when unsharing.
        let unshare = store.frame();
        unshare.add(n_caseid, this.caseid());
        unshare.add(n_share, false);
        data = unshare.encode();
      }

      // Send case to server.
      let r = await fetch("/case/share", {
        method: 'POST',
        headers: {
          'Content-Type': 'application/sling'
        },
        body: data
      });
      if (!r.ok) {
        console.log("Sharing error", r);
        StdDialog.error(`Error ${r.status} sharing case: ${r.statusText}`);
      }
    }
  }

  async oncollaborate(e) {
    if (this.readonly) return;
    if (this.collab) return this.oninvite(e)
    if (!this.main.has(n_author)) {
      inform("No case author defined. Please add yourself as case author.");
      return;
    }

    let dialog = new CollaborateDialog(settings.collaburl);
    let url = await dialog.show();
    if (url) {
      // Connect to collaboration server and create new collaboration.
      let collab = new Collaboration();
      await collab.connect(url);
      let credentials = await collab.create(this.casefile);
      collab.close();

      // Add collaboration to case.
      this.casefile.set(n_collaborate, true);
      this.casefile.set(n_collab, url);
      this.casefile.set(n_userid, this.main.get(n_author).id);
      this.casefile.set(n_credentials, credentials);
      this.casefile.remove(n_topics);
      this.casefile.remove(n_folders);
      this.casefile.remove(n_next);

      // Save and reload.
      this.match("#app").save_case(this.casefile);
      await this.update(this.casefile);

      inform("Case has been turned into a collaboration. "+
             "You can now invite participants to join.");
    }
  }

  async oninvite(e) {
    if (!this.collab) return;
    let dialog = new InviteDialog();
    let participant = await dialog.show();
    if (participant) {
      // Add invited user as participant.
      if (this.main.put(n_participant, frame(participant))) {
        this.update_topic(this.main);
        this.topic_updated(this.main);
      }

      // Get invite key from collaboration server.
      let key = await this.collab.invite(participant);
      let base = window.location.href;
      let frag = `collab=${this.collab.url},as=${participant},invite=${key}`;
      let url = base + "#" + frag;
      navigator.clipboard.writeText(url);
      inform(`Invite URL for participant has been copied to the clipboard`);
    }
  }

  async onimgcache(e) {
    // Collect media in case.
    let media = new Array();
    for (let topic of this.topics) {
      for (let m of topic.all(n_media)) {
        let url = store.resolve(m);
        let nsfw = url.startsWith('!');
        if (nsfw) url = url.slice(1);
        media.push(url);
      }
    }

    // Send image caching request to server.
    let r  = await fetch("/case/cacheimg", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify({media}),
    });
    if (!r.ok) {
      console.log("Caching error", r);
      inform(`Error ${r.status}: ${r.statusText}`);
    } else {
      inform("Images are being cached in the background");
    }
  }

  async onupdate() {
    if (!this.state) return;
    this.mark_clean();
    this.index = null;
    this.linksindex = new Map();
    this.casefile = this.state;

    // Connect to collaboration server for collaboration case.
    let collab_url = this.casefile.get(n_collab)
    if (this.casefile.get(n_collab)) {
      // Connect to collaboration server.
      this.collab = new Collaboration();
      await this.collab.connect(collab_url);

      // Login to collaboration.
      let caseid = this.casefile.get(n_caseid);
      let userid = this.casefile.get(n_userid);
      let credentials = this.casefile.get(n_credentials);
      this.localcase = this.casefile;
      this.casefile = await this.collab.login(caseid, userid, credentials);

      // Listen on remote collaboration case updates.
      this.collab.listener = this;

      // Update local case.
      for (let prop of [n_created, n_modified, n_main]) {
        this.localcase.set(prop, this.casefile.get(prop));
      }
      this.match("#app").save_case(this.localcase);
    } else {
      this.collab = undefined;
      this.localcase = undefined;
    }

    // Initialize case editor for new case.
    this.main = this.casefile.get(n_main);
    this.topics = this.casefile.get(n_topics);
    this.folders = this.casefile.get(n_folders);
    this.folder = this.folders.value(0);
    this.scraps = [];
    this.readonly = this.casefile.get(n_link);

    // Read linked cases.
    this.links = [];
    for (let topic of this.topics) {
      if (topic != this.main && topic.get(n_instance_of) == n_case_file) {
        let linkid = topic.link().id;
        let caseid = parseInt(linkid.substring(2));
        let casefile = await this.match("#app").read_case(caseid);
        if (casefile) {
          this.links.push(casefile);
        } else {
          console.log("Unable to retrieve linked case", linkid);
        }
      }
    }

    // Update title.
    this.update_title();

    // Enable/disable action buttons.
    for (let e of ["#save", "#share", "#merge", "#wikiexport"]) {
      this.find(e).update(!this.readonly);
    }
    if (this.collab) {
      this.find("#save").update(false);
      this.find("#invite").update(true);
    } else {
      this.find("#invite").update(false);
    }
    this.find("#menu").style.display = this.readonly ? "none" : "";
    this.find("#addlink").update(this.readonly);
    this.find("#script").update(settings.userscripts);
  }

  update_title() {
    window.document.title = `#${this.caseid()} ${this.main.get(n_name)}`;
  }

  async onupdated() {
    if (this.state) {
      this.find("#caseid").update(this.caseid().toString());
      await this.update_folders();
      await this.update_topics();
      await this.navigate_to(this.main);
    }
  }

  caseid() {
    return this.casefile.get(n_caseid);
  }

  name() {
    return this.main.get(n_name);
  }

  encoded() {
    let encoder = new Encoder(store);
    for (let topic of this.casefile.get(n_topics)) {
      encoder.encode(topic);
    }
    encoder.encode(this.casefile);
    return encoder.output();
  }

  async next_topic() {
    if (this.collab) {
      return await this.collab.nextid();
    } else {
      let next = this.casefile.get(n_next);
      this.casefile.set(n_next, next + 1);
      return next;
    }
  }

  mark_clean() {
    if (this.collab) return;
    this.dirty = false;
    this.find("#save").disable();
  }

  mark_dirty() {
    this.index = null;
    if (this.collab) return;
    this.dirty = true;
    this.find("#save").enable();
  }

  close() {
    sidebar.update();
    if (this.collab) {
      this.collab.close();
      this.collab = undefined;
      this.localcase = undefined;
      app.show_manager();
    } else if (this.dirty) {
      let msg = `Changes to case #${this.caseid()} has not been saved.`;
      let buttons = {
        "Close without saving": "close",
        "Cancel": "cancel",
        "Save": "save",
      }
      StdDialog.choose("Discard changes?", msg, buttons)
      .then(result => {
        if (result == "save") {
          this.match("#app").save_case(this.casefile);
          this.mark_clean();
          app.show_manager();
        } else if (result == "close") {
          this.mark_clean();
          app.show_manager();
        }
      });
    } else {
      app.show_manager();
    }
  }

  folder_index(folder) {
    let f = this.folders;
    for (let i = 0; i < f.length; ++i) {
      if (f.value(i) === folder) return i;
    }
  }

  folder_name(folder) {
    let f = this.folders;
    for (let i = 0; i < f.length; ++i) {
      if (f.value(i) === folder) return f.name(i);
    }
  }

  refcount(topic) {
    let refs = 0;
    let f = this.folders;
    for (let i = 0; i < f.length; ++i) {
      if (f.value(i).includes(topic)) refs += 1;
    }
    return refs;
  }

  find_topic(item) {
    for (let topic of this.topics) {
      if (topic == item ||
          topic.has(n_is, item) ||
          topic.has(n_is, item.id)) {
        return topic;
      }
    }
  }

  selection() {
    let list = this.find("topic-list");
    if (!list) return;
    return list.selection();
  }

  selected() {
    let selection = this.selection();
    if (!selection || selection.length != 1) return;
    return selection[0];
  }

  redirect(source, target) {
    let topics = this.topics;
    if (this.scraps.length > 0) topics = topics.concat(this.scraps);
    for (let topic of topics) {
      if (topic === target) continue;
      let updated = false;
      for (let n = 0; n < topic.length; ++n) {
        let name = topic.value(n);
        let value = topic.value(n);
        if (source == value) {
          topic.set_value(n, target);
          updated = true;
        } else if (name == n_is && value == source.id) {
          topic.set_value(n, target.id);
          updated = true;
        } else if (value instanceof Frame) {
          if (value.isanonymous() && value.has(n_is)) {
            for (let m = 0; m < value.length; ++m) {
              if (source == value.value(m)) {
                value.set_value(m, target);
               updated = true;
              }
            }
          }
        }
      }
      if (updated && !this.scraps.includes(topic)) this.topic_updated(topic);
    }

    if (this.sidebar) {
      let doc = this.sidebar.state;
      if (doc) {
        let replacement;
        if (replacement instanceof Frame) replacement = target;
        for (let m of doc.mentions) {
          if (m.annotation instanceof Frame) {
            if (m.annotation == source) {
              m.annotation = replacement;
            } else if (m.annotation.get(n_is) == source) {
              m.annotation.set(n_is) = replacement;
            }
          }
        }
      }
    }
  }

  next_folder() {
    let current = this.folder_index(this.folder);
    let next = this.folders.value(current + 1);
    if (next) this.show_folder(next);
  }

  prev_folder() {
    let current = this.folder_index(this.folder);
    let prev = this.folders.value(current - 1);
    if (prev) this.show_folder(prev);
  }

  async show_folder(folder) {
    if (folder != this.folder) {
      this.folder = folder;
      await this.update_folders();
      await this.find("topic-list").refresh(this.folder);
      if (folder.length > 0) {
        await this.navigate_to(folder[0]);
      }
    }
  }

  add_folder(name) {
    if (this.readonly) return;
    for (let i = 0; i < this.folders.length; ++i) {
      if (this.folders.name(i) == name) {
        inform(`Folder "${name}" already exists`);
        return;
      }
    }

    this.folder = new Array();
    this.folders.add(name, this.folder);
    this.mark_dirty();
    this.folders_updated();
    this.update_folders();
    this.update_topics();
  }

  rename_folder(folder, name) {
    if (this.readonly) return;
    for (let i = 0; i < this.folders.length; ++i) {
      if (this.folders.name(i) == name) {
        inform(`Folder "${name}" already exists`);
        return;
      }
    }

    let pos = this.folder_index(folder);
    if (pos > 0 && pos < this.folders.length) {
      this.folder_renamed(folder, name);
      this.folders.set_name(pos, name);
      this.mark_dirty();
      this.update_folders();
    }
  }

  move_folder_up(folder) {
    if (this.readonly) return;
    let pos = this.folder_index(folder);
    if (pos > 0 && pos < this.folders.length) {
      // Swap with previous folder.
      let tmp_name = this.folders.name(pos);
      let tmp_value = this.folders.value(pos);
      this.folders.set_name(pos, this.folders.name(pos - 1));
      this.folders.set_value(pos, this.folders.value(pos - 1));
      this.folders.set_name(pos - 1, tmp_name);
      this.folders.set_value(pos - 1, tmp_value);
      this.folders_updated();
      this.mark_dirty();
      this.update_folders();
    }
  }

  move_folder_down(folder) {
    if (this.readonly) return;
    let pos = this.folder_index(folder);
    if (pos >= 0 && pos < this.folders.length - 1) {
      // Swap with next folder.
      let tmp_name = this.folders.name(pos);
      let tmp_value = this.folders.value(pos);
      this.folders.set_name(pos, this.folders.name(pos + 1));
      this.folders.set_value(pos, this.folders.value(pos + 1));
      this.folders.set_name(pos + 1, tmp_name);
      this.folders.set_value(pos + 1, tmp_value);
      this.folders_updated();
      this.mark_dirty();
      this.update_folders();
    }
  }

  delete_folder(folder) {
    if (this.readonly) return;
    if (folder.length != 0) {
      StdDialog.alert("Cannot delete folder",
                      "Folder must be empty to be deleted");
    } else {
      let pos = this.folder_index(folder);
      if (pos >= 0 && pos < this.folders.length) {
        this.folders.remove(pos);
        if (pos == this.folders.length) {
          this.folder = this.folders.value(pos - 1);
        } else {
          this.folder = this.folders.value(pos);
        }
        this.folders_updated();
        this.mark_dirty();
        this.update_folders();
        this.update_topics();
      }
    }
  }

  add_topic(topic, position) {
    // Add topic to current folder.
    if (this.readonly) return;
    if (this.folder == this.scraps) {
      inform("new topic cannot be added to scraps folder");
      return;
    }
    this.topic_updated(topic);
    if (!this.topics.includes(topic)) {
      this.topics.push(topic);
    }
    if (!this.folder.includes(topic)) {
      if (position && this.folder.includes(position)) {
        position = this.folder.indexOf(position) + 1;
      }
      if (typeof(position) === 'number' && position >= 0) {
        this.folder.splice(position, 0, topic);
      } else {
        this.folder.push(topic);
      }
      this.folder_updated(this.folder);
    }
    this.mark_dirty();
  }

  async new_topic(topic, position) {
    // Create frame for new topic.
    if (this.readonly) return;
    let topicno = await this.next_topic();
    let topicid = `t/${this.caseid()}/${topicno}`;
    if (topic) {
      topic.assign(topicid);
    } else {
      topic = store.frame(topicid);
    }

    // Add topic to current folder.
    this.add_topic(topic, position);

    return topic;
  }

  async add_new_topic(itemid, name) {
    // Create new topic.
    if (this.readonly) return;
    let topic = await this.new_topic();
    if (itemid) topic.add(n_is, itemid);
    if (name) topic.add(n_name, name);
    this.topic_updated(topic);

    // Update topic list.
    await this.update_topics();
    this.navigate_to(topic);
  }

  async add_case_link(caserec) {
    // Create new topic with reference to external case.
    let topic = await this.new_topic();
    topic.add(n_is, frame(`c/${caserec.id}`));
    topic.add(n_instance_of, n_case_file);
    if (caserec.name) topic.add(n_name, `Case #${caserec.id}: ${caserec.name}`);
    this.topic_updated(topic);

    // Read case and add linked case.
    let casefile = await this.match("#app").read_case(caserec.id);
    if (casefile) {
      this.links.push(casefile);
    }

    // Update topic list.
    await this.update_topics();
    this.navigate_to(topic);
  }

  async add_topic_link(topic, position) {
    // Create new topic with reference to topic in external case.
    let link = await this.new_topic(null, position);
    link.add(n_is, topic.id);
    let name = topic.get(n_name);
    if (name) link.add(n_name, name);
    this.topic_updated(link);

    // Update topic list.
    await this.update_topics();
    return link;
  }

  async delete_topics(topics) {
    if (this.readonly || topics.length == 0) return;

    // Focus should move to first topic after selection.
    let next = null;
    let last = this.folder.indexOf(topics.at(-1));
    if (last + 1 < this.folder.length) next = this.folder[last + 1];

    // Delete topics from folder.
    let scraps_before = this.scraps.length > 0;
    for (let topic of topics) {
      // Do not delete main topic.
      if (topic == this.casefile.get(n_main)) return;

      // Delete topic from current folder.
      let is_scrap = this.scraps.includes(topic);
      let pos = this.folder.indexOf(topic);
      if (pos != -1) {
        this.folder.splice(pos, 1);
      }

      // Delete topic from case.
      if (this.refcount(topic) == 0) {
        if (is_scrap) {
          // Delete draft topic from case and redirect all references to it.
          console.log("purge topic", topic.id);
          this.purge_topic(topic);
        } else {
          // Move topic to scraps.
          let pos = this.topics.indexOf(topic);
          if (pos != -1) {
            this.topics.splice(pos, 1);
          } else {
            console.log("topic not found", topic.id);
          }
          this.scraps.push(topic);
          this.topic_deleted(topic);
        }
      }
    }
    this.mark_dirty();

    // Update folder list if scraps was added.
    let scraps_after = this.scraps.length > 0;
    if (scraps_before != scraps_after) await this.update_folders();

    // Update topic list and navigate to next topic.
    this.folder_updated(this.folder);
    await this.update_topics();
    if (next) {
      await this.navigate_to(next);
    } else if (this.folder.length > 0) {
      await this.navigate_to(this.folder.at(-1));
    }
  }

  delete_topic(topic) {
    return this.delete_topics([topic]);
  }

  async purge_topic(topic) {
    let target = topic.link();
    if (!target) target = topic.get(n_name);
    if (!target) target = topic.id;
    this.redirect(topic, target);
  }

  async purge_scraps() {
    if (this.scraps.length > 0) {
      // Remove all references to draft topics.
      for (let topic of this.scraps) {
        this.purge_topic(topic);
      }

      // Remove scraps.
      this.scraps.length = 0;

      // Update folders.
      if (this.folder == this.scraps) {
        await this.navigate_to(this.main);
      }
      await this.update_folders();
    }
  }

  async move_topic_up(topic) {
    if (this.readonly) return;
    let pos = this.folder.indexOf(topic);
    if (pos == -1) return;
    if (pos == 0 || this.folder.length == 1) return;

    // Swap with previous topic.
    let tmp = this.folder[pos];
    this.folder[pos] = this.folder[pos - 1];
    this.folder[pos - 1] = tmp;
    this.folder_updated(this.folder);
    this.mark_dirty();

    // Update topic list.
    await this.update_topics();
    await this.navigate_to(topic);
  }

  async move_topic_down(topic) {
    if (this.readonly) return;
    let pos = this.folder.indexOf(topic);
    if (pos == -1) return;
    if (pos == this.folder.length - 1 || this.folder.length == 1) return;

    // Swap with next topic.
    let tmp = this.folder[pos];
    this.folder[pos] = this.folder[pos + 1];
    this.folder[pos + 1] = tmp;
    this.folder_updated(this.folder);
    this.mark_dirty();

    // Update topic list.
    await this.update_topics();
    await this.navigate_to(topic);
  }

  async oncut(e) {
    // Get selected topics.
    if (this.readonly) return;
    if (this.folder == this.scraps) return this.oncopy(e);
    let selected = this.selection();
    if (selected.length == 0) return;
    console.log(`cut ${selected.length} topics to clipboard`);

    // Copy selected topics to clipboard.
    await write_to_clipboard(selected);

    // Delete selected topics.
    this.delete_topics(selected);
  }

  async oncopy(e) {
    // Allow copying of selected text.
    let s = window.getSelection();
    if (!s.isCollapsed) {
      let anchor_text = s.anchorNode && s.anchorNode.nodeType == Node.TEXT_NODE;
      let focus_text = s.focusNode && s.focusNode.nodeType == Node.TEXT_NODE;
      if (anchor_text && focus_text) return;
    }

    // Get selected topics.
    let selected = this.selection();
    if (selected.length == 0) return;
    e.stopPropagation();

    // Copy selected topics to clipboard.
    await write_to_clipboard(selected);
  }

  oncopyall(e) {
    // Copy all topics (except the main topic) to clipboard.
    write_to_clipboard(this.topics.filter(topic => topic != this.main));
  }

  async onpaste(e) {
    // Allow pasting of text into text input.
    let focus = document.activeElement;
    if (focus) {
     if (focus.type == "search") return;
     if (focus.type == "textarea") return;
    }
    e.preventDefault();
    e.stopPropagation();

    // Paste not allowed in scraps folder.
    if (this.folder == this.scraps) return;

    // Read topics from clipboard into a new store.
    if (this.readonly) return;
    let clip = await read_from_clipboard();

    // Add topics to current folder if clipboard contains frames.
    if (clip instanceof Array) {
      let first = null;
      let last = null;
      let position = this.find("topic-list").selection_start() + 1;
      if (position === undefined) position = this.folder.length;
      let scraps_before = this.scraps.length > 0;
      let import_mapping = new Map();
      for (let t of clip) {
        // Determine if pasted topic is from this case.
        let topic = store.find(t.id);
        let external = true;
        if (topic) {
          let in_topics = this.topics.includes(topic);
          if (in_topics) {
            // Add link to topic in current folder.
            this.add_topic(topic, position++);
            external = false;
          } else {
            let draft_pos = this.scraps.indexOf(topic);
            if (draft_pos != -1) {
              // Move topic from scraps to current folder.
              this.add_topic(topic, position++);
              this.scraps.splice(draft_pos, 1);
              external = false;
            }
          }
        }

        // Copy topic if it is external.
        if (external) {
          topic = await this.new_topic(null, position);
          import_mapping.set(t.id, topic);
          for (let [name, value] of t) {
            if (name != t.store.id) {
              topic.add(store.transfer(name), store.transfer(value));
            }
          }
          this.topic_updated(topic);
        }

        if (!first) first = topic;
        last = topic;
      }

      // Redirect imported topic ids.
      for (let [id, topic] of import_mapping.entries()) {
        let proxy = store.find(id);
        if (proxy) this.redirect(proxy, topic);
        topic.apply((name, value) => {
          if (name == n_is) {
            let redirect = import_mapping.get(value);
            if (redirect) return [name, redirect.id];
          }
        });
      }

      // Update topic list.
      let scraps_after = this.scraps.length > 0;
      if (scraps_before != scraps_after) await this.update_folders();
      await this.update_topics();
      if (first && last) {
        let list = this.find("topic-list");
        list.select_range(first, last);
        list.card(last).focus();
      }

      return;
    }

    // Let the plug-ins process the clipboard content.
    let list = this.find("topic-list");
    let topic = list.active();
    clip = await navigator.clipboard.readText();
    if (clip) {
      this.style.cursor = "wait";
      let context = new plugins.Context(topic, this.casefile, this);
      var result;
      try {
        result = await plugins.process(plugins.PASTE, clip, context);
      } catch (e) {
        inform("Paste error: " + e.toString());
        console.log(e);
        this.style.cursor = "";
        return;
      }
      this.style.cursor = "";
      if (result) {
        await context.refresh();
        return;
      }
    }

    // Try to paste image.
    if (topic) {
      let imgurl = await Drive.paste_image();
      if (imgurl) {
        topic.add(n_media, imgurl);
        this.topic_updated(topic);
        this.mark_dirty();
        await this.update_topic(topic);
        return;
      }
    }
  }

  async onmerge(e) {
    // Get selected topics.
    if (this.readonly) return;
    let selected = this.selection();
    if (selected.length == 1) {
      let focus = selected[0];
      for (let redirect of focus.links()) {
        if (this.topics.includes(redirect)) {
          // Merge with redirect.
          selected.unshift(redirect);
        } else {
          for (let t of this.topics) {
            if (t != focus && t.link() == redirect) {
              // Merge with topic with identical redirect.
              selected.unshift(t);
              break;
            }
          }
        }
        if (selected.length > 1) break;
      }
    }
    if (selected.length < 2) return;

    // Merge the rest of the topics into the first topic.
    let target = selected[0];
    let sources = selected.slice(1);
    let updated_folders = new Set();
    for (let topic of sources) {
      // Add properties from topic to target.
      for (let [name, value] of topic) {
        if (name == store.id) continue;
        if (name == store.is) {
          let link = value;
          if (typeof link === 'string') link = frame(link);
          if (selected.includes(link)) continue;
        }
        target.put(name, value);
      }

      // Redirect references to topic to target.
      this.redirect(topic, target);

      // Replace source with target in all folders. Delete it if target is in
      // the same folder.
      for (let [n, f] of this.folders) {
        let pos = f.indexOf(topic);
        if (pos != -1) {
          if (f.includes(target)) {
            f.splice(pos, 1);
          } else {
            f[pos] = target;
          }
          updated_folders.add(f);
        }
      }
    }

    // Delete merged topics from folder.
    await this.delete_topics(sources);

    // Update folders.
    for (let f of updated_folders) {
      this.folder_updated(f);
    }

    // Update target topic.
    this.topic_updated(target);
    this.mark_dirty();
    let card = list.card(target);
    if (card) {
      card.update(target);
      card.refresh(target);
      await this.update_topics();
      await this.navigate_to(target);
    }
  }

  async onscript(e) {
    if (!settings.userscripts) return;
    let dialog = new ScriptDialog();
    let script = await dialog.show();
    if (script) {
      // Execute script.
      try {
        if (await script.call(this, store, frame, this.print.bind(this))) {
          // Update editor.
          this.mark_dirty();
          await this.update_folders();
          await this.refresh_topics();
        }
      } catch(e) {
        console.log("Script error", e);
        StdDialog.error(`Error executing script: ${e.message}`);
      }
      if (this.log) {
        StdDialog.info("Script output", this.log.join(" "));
      }
      this.log = null;
    }
  }

  async onupload(e) {
    if (this.readonly) return;
    let dialog = new UploadDialog();
    let files = await dialog.show();
    if (files) {
      var topic;
      let now = new Date();
      for (let file of files) {
        // Save file to drive.
        let url = await Drive.save(file);

        // Add topic for file.
        let isimage = media_file_types.includes(file.type);
        topic = await this.new_topic();
        topic.add(n_name, file.name);
        topic.add(n_instance_of, isimage ? n_image : n_document);
        if (file.lastModified) {
          let date = new Date(file.lastModified);
          topic.add(n_publication_date, date_number(date));
        }
        topic.add(n_retrieved, date_number(now));
        topic.add(n_url, url);
        topic.add(n_data_size, file.size);
        if (file.type) {
          topic.add(n_media_type, file.type);
        }
        if (isimage) {
          topic.add(n_media, url);
        }
        this.topic_updated(topic);
      }

      // Update topic list.
      await this.update_topics();
      this.navigate_to(topic);

      inform(`${files.length} files uploaded`);
    }
  }

  async onwikiexport(e) {
    if (this.readonly) return;

    // Initiate OAuth authorization if we don't have an access token.
    if (!settings.wikidata_key) {
      let ok = await StdDialog.confirm("Wikidata export",
        "Before you can publish topics in Wikidata you need to authorize " +
        "SLING to make changes in Wikidata on your behalf. You will now be " +
        "directed to wikidata.org for authorization.");
      if (ok) wikidata_initiate();
      return;
    }

    // Get list of topic to export, either selection or all topics.
    let topics = this.selection();
    if (topics.length > 0) {
      // Do not allow exporting case topics.
      for (let topic of topics) {
        if (topic.has(n_instance_of, n_case_file)) {
          inform("Main case topic cannot be published in Wikidata");
          return;
        }
      }
    } else {
      // Do not export from NSFW cases.
      if (this.main.has(n_has_quality, n_nsfw)) {
        inform("NSFW cases cannot be published in Wikidata");
        return;
      }

      // Ask before publishing all topics.
      let ok = await StdDialog.ask("Wikidata export",
                                   "Publish all case topics in Wikidata?");
      if (!ok) return;

      // Export all topics if there is no selection.
      for (let topic of this.topics) {
        if (topic.has(n_instance_of, n_case_file)) continue;
        topics.push(topic);
      }
    }
    if (topics.length == 0) return;

    // Add all referenced topics as auxiliary topics.
    let aux = new Set();
    for (let topic of topics) {
      for (let [name, value] of topic) {
        if (!(value instanceof Frame)) continue;
        let ref = value;
        let qualified = false;
        if (value.isanonymous() && value.has(n_is)) {
          ref = value.get(n_is);
          qualified = true;
        }

        if (ref instanceof Frame) {
          if (this.topics.includes(ref) && !topics.includes(ref)) {
            aux.add(ref);
          }
        }

        if (qualified) {
          for (let [qname, qvalue] of value) {
            if (qvalue instanceof Frame) {
              if (this.topics.includes(qvalue) && !topics.includes(qvalue)) {
                aux.add(qvalue);
              }
            }
          }
        }
      }
    }

    // Try to resolve topics to Wikidata items.
    let dirty = false;
    let collector = new ItemCollector(store);
    for (let t of topics) collector.add_links(t);
    for (let t of aux) collector.add_links(t);
    await collector.retrieve();
    for (let t of [...topics, ...aux]) {
      let qid = null;
      for (let link of t.links()) {
        if (link.id.startsWith("Q")) {
          qid = link.id;
          break;
        }
      }
      if (qid) {
        if (t.put(n_is, qid)) {
          this.update_topic(t);
          this.topic_updated(t);
          dirty = true;
        }
      }
    }

    // Export topics.
    this.style.cursor = "wait";
    inform(`Publishing ${topics.length} topics to Wikidata`);
    try {
      let [updated, status] = await wikidata_export(topics, aux);
      if (updated.length > 0) {
        for (let topic of updated) {
          this.update_topic(topic);
          this.topic_updated(topic);
        }
        dirty = true;
      }
      inform("Published in Wikidata: " + status);
    } catch (e) {
      inform(e.name + ": " + e.message);
    }
    this.style.cursor = "";
    if (dirty) this.mark_dirty();
  }

  async onimport(e) {
    if (this.readonly) return;
    try {
      await importers.import_data(this.casefile, this);
      this.mark_dirty();
      await this.update_folders();
      await this.refresh_topics();
    } catch (error) {
      inform("Error importing data: " + error);
    }
  }

  async onfilesave(e) {
    try {
      // Get filename for exporting case.
      let fh = await window.showSaveFilePicker({
        suggestedName: `${this.name()}.sling`});

      // Serialize case.
      let printer = new Printer(store);
      printer.indent = "  ";
      for (let topic of this.topics) printer.print(topic);
      printer.print(this.casefile);

      // Save case to file.
      let fw = await fh.createWritable();
      await fw.write(printer.output);
      await fw.close();
    } catch (error) {
      inform("Error saving case to file: " + error);
    }
  }

  onstats(e) {
    // Tally up case statistics.
    let num_topics = 0;
    let num_facts = 0;
    let num_folders = this.folders.length;
    let num_topics_in_folder = this.folder.length;
    let num_names = 0;
    let num_comments = 0;
    let num_pictures = 0;
    let num_with_pictures = 0;
    let num_nsfw_pictures = 0;
    let num_redirs = 0;
    for (let t of this.topics) {
      num_topics += 1;
      let has_picture = false;
      for (let [name, value] of t) {
        if (name == n_name || name == n_alias) {
          num_names += 1;
        } if (name == null) {
          num_comments += 1;
        } if (name == n_media) {
          num_pictures += 1;
          has_picture = true;
          let url = store.resolve(value);
          if (url.startsWith("!")) num_nsfw_pictures += 1;
        } if (name == n_is) {
          num_redirs += 1;
        } if (name != n_id) {
          num_facts += 1;
        }
      }
      if (has_picture) num_with_pictures += 1;
    }

    let created = new Date(this.casefile.get(n_created));
    let modified = new Date(this.casefile.get(n_modified));

    function number(n, kind) {
      return `${n} ${kind}${kind == 1 ? "" : "s"}`;
    }

    let message = `
      created: ${created.toDateString()}
      modified: ${modified.toDateString()}

      ${number(num_topics, "topic")}
      ${number(num_redirs, "existing topic")}
      ${number(num_topics - num_redirs, "new topic")}
      ${number(num_with_pictures, "topic")} with pictures
      ${number(num_topics_in_folder, "topic")} in folder
      ${number(num_facts, "fact")}
      ${number(num_folders, "folder")}
      ${number(num_names, "name")}
      ${number(num_comments, "note")}
      ${number(num_pictures, "picture")}
      ${number(num_nsfw_pictures, "NSFW picture")}
    `;
    StdDialog.info("Case statistics", message);
  }

  async onaddlink(e) {
    let ok = await StdDialog.confirm(
      "Create linked case",
      "Create you own case linked to this case?");
    if (ok) {
      let linkid = this.main.id;
      let name = this.main.get(n_name);
      this.match("#app").add_case(name, null, linkid, n_case_file);
    }
  }

  async check_rights(topic) {
    if (!this.readonly) return true;
    if (!topic.has(n_copyright, n_copyrighted)) return true;
    let topicid = topic.id;
    if (work_ownership.has(topicid)) return true;
    let title = topic.get(n_name);
    let answer = await StdDialog.choose(
      "Copyrighted content",
      `'${title}' is a copyrighted work. Please confirm that you own a copy.`,
      {"Cancel": 0, "I own a copy": 10-4},
      "warning",
      "color: orange");
    if (answer != 10-4) return false;
    let r = await fetch(`/case/ownswork?work=${topicid}`, {method: "POST"});
    if (!r.ok) return false;
    work_ownership.add(topicid);
    return true;
  }

  store() {
    return store;
  }

  print() {
    if (!this.log) this.log = new Array();
    for (let msg of arguments) {
      if (msg === null) {
        this.log.push("null");
      } else if (msg === undefined) {
        this.log.push("undefined");
      } else {
        this.log.push(msg.toString());
      }
    }
    this.log.push("\n");
  }

  fetch(resource, init) {
    return fetch(`/case/proxy?url=${encodeURIComponent(resource)}`, init);
  }

  async collect(topics) {
    let collector = new ItemCollector(store);
    for (let t of topics) {
      collector.add(store.resolve(t));
    }
    return await collector.retrieve();
  }

  async label(topics) {
    let collector = new LabelCollector(store);
    for (let t of topics) {
      collector.add_item(store.resolve(t));
    }
    return await collector.retrieve();
  }

  topic_updated(topic) {
    if (this.collab) {
      this.collab.topic_updated(topic);
    }
  }

  topic_deleted(topic) {
    if (this.collab) {
      this.collab.topic_deleted(topic);
    }
  }

  folder_updated(folder) {
    if (this.collab && folder != this.scraps) {
      this.collab.folder_updated(this.folder_name(folder), folder);
    }
  }

  folder_renamed(folder, name) {
    if (this.collab) {
      this.collab.folder_renamed(this.folder_name(folder), name);
    }
  }

  folders_updated() {
    if (this.collab) {
      this.collab.folders_updated(this.folders);
    }
  }

  remote_topic_update(topic) {
    console.log("received topic update", topic.id);
    if (!this.topics.includes(topic)) this.topics.push(topic);
    this.update_topic(topic);
  }

  remote_folder_update(name, topics) {
    console.log("received folder update", name);
    let f = this.folders;
    for (let i = 0; i < f.length; ++i) {
      if (f.name(i) == name) {
        let folder = f.value(i);
        folder.length = 0;
        folder.push(...topics);
        if (folder == this.folder) this.update_topics();
      }
    }
  }

  remote_folders_update(folders) {
    console.log("received folder list update", folders.length);
    let foldermap = new Map();
    for (let [name, content] of this.folders) {
      foldermap.set(name, content);
    }
    this.folders = store.frame();
    for (let name of folders) {
      let topics = foldermap.get(name);
      if (!topics) topics = new Array();
      this.folders.add(name, topics);
    }
    this.update_folders();
  }

  remote_topic_delete(topicid) {
    console.log("received topic delete", topicid);
    let topic = frame(topicid);
    let pos = this.topics.indexOf(topic);
    if (pos != -1) this.topics.splice(pos, 1);
  }

  remote_folder_rename(oldname, newname) {
    console.log("received folder rename", oldname, newname);
    this.folders.apply((name, content) => {
      if (name == oldname) return [newname, content];
    });
    this.update_folders();
  }

  remote_save(modtime) {
    console.log("received save", modtime);
    this.casefile.set(n_modified, modtime);
  }

  async remote_closed(collab) {
    inform("Connection to collaboration server lost");
    let reconnect = await StdDialog.ask("Connection lost",
                                        "Reconnect to collaboration server?");
    if (reconnect) location.reload();
  }

  remote_error(collab) {
    inform("Error communicating with collaboration server");
  }

  async update_folders() {
    await this.find("drawer-panel").update({
      folders: this.folders,
      current: this.folder,
      scraps: this.scraps,
      readonly: this.readonly
    });
  }

  async update_topics() {
    await this.find("topic-list").update(this.folder);
  }

  async refresh_topics() {
    await this.find("topic-list").refresh(this.folder);
  }

  async update_topic(topic) {
    let list = this.find("topic-list");
    let card = list.card(topic);
    if (card) card.refresh();
  }

  async navigate_to(topic) {
    if (!this.folder.includes(topic)) {
      // Switch to folder with topic.
      let folder = null;
      for (let [n, f] of this.folders) {
        if (f.includes(topic)) {
          folder = f;
          break;
        }
      }
      if (!folder && this.scraps.includes(topic)) {
        folder = this.scraps;
      }
      if (folder)  {
        await this.show_folder(folder);
      }
    }

    // Scroll to topic in folder.
    return await this.find("topic-list").navigate_to(topic);
  }

  prerender() {
    return `
      <md-toolbar>
        <md-icon-button id="drawer" icon="menu"></md-icon-button>
        <md-toolbar-logo></md-toolbar-logo>
        <div id="title">Case #<md-text id="caseid"></md-text></div>
        <omni-box id="search"></omni-box>
        <md-spacer></md-spacer>
        <md-icon-button
          id="merge"
          class="tool"
          icon="merge"
          tooltip="Merge topics\n(Ctrl+M)">
        </md-icon-button>
        <md-icon-button
          id="wikiexport"
          class="tool"
          icon="wikidata"
          tooltip="Export to Wikidata">
        </md-icon-button>
        <md-icon-button
          id="script"
          class="tool"
          icon="play_circle_outline"
          tooltip="Execute script">
        </md-icon-button>
        <md-icon-button
          id="addlink"
          class="tool"
          icon="playlist_add"
          tooltip="Open new linked case">
        </md-icon-button>
        <md-icon-button
          id="save"
          class="tool"
          icon="save"
          tooltip="Save case\n(Ctrl+S)">
        </md-icon-button>
        <md-icon-button
          id="invite"
          class="tool"
          icon="people"
          tooltip="Invite participant">
        </md-icon-button>
        <md-icon-button
          id="share"
          class="tool"
          icon="share"
          tooltip="Share case">
        </md-icon-button>
        <md-icon-button
          id="home"
          class="tool"
          icon="home"
          tooltip="Go to case list"
          tooltip-align="right">
        </md-icon-button>
        <md-menu id="menu">
          <md-menu-item id="save">Save</md-menu-item>
          <md-menu-item id="filesave">Save to file</md-menu-item>
          <md-menu-item id="share">Share</md-menu-item>
          <md-menu-item id="collaborate">Collaborate</md-menu-item>
          <md-menu-item id="import">Import from file</md-menu-item>
          <md-menu-item id="upload">Upload files</md-menu-item>
          <md-menu-item id="wikiexport">Publish in Wikidata</md-menu-item>
          <md-menu-item id="imgcache">Cache images</md-menu-item>
          <md-menu-item id="copyall">Copy all</md-menu-item>
          <md-menu-item id="stats">Statistics</md-menu-item>
        </md-menu>
      </md-toolbar>

      <div id="container">
        <drawer-panel></drawer-panel>
        <md-content>
          <topic-list></topic-list>
        </md-content>
        <side-bar id="sidebar"></side-bar>
      </md-row-layout>
    `;
  }

  static stylesheet() {
    return `
      $ md-toolbar {
        padding-left: 2px;
      }
      $ #title {
        white-space: nowrap;
      }
      $ #container {
        display: flex;
        flex-direction: row;
        overflow: auto;
        height: 100%;
        width: 100%;
        min-width: 100%;

      }
      $ md-toolbar md-icon-button.tool {
        margin-left: -8px;
      }
      $ md-toolbar md-menu {
        color: black;
      }
      $ md-toolbar md-menu md-icon-button {
        margin-left: inherit;
      }
      $ topic-list md-icon {
        color: #808080;
      }
      $ topic-list md-icon-button {
        color: #808080;
        fill: #808080;
      }
    `;
  }
}

Component.register(CaseEditor);

class SharingDialog extends MdDialog {
  onconnected() {
    if (this.state.publish) {
      this.find("#publish").update(true);
    } else if (this.state.secret) {
      this.find("#restrict").update(true);
    } else if (this.state.share) {
      this.find("#share").update(true);
    } else {
      this.find("#private").update(true);
    }

    this.secret = this.state.secret;
    this.url = window.location.href;
    this.find("#sharingurl").update(this.sharingurl());

    for (let checkbox of ["#private", "#share", "#restrict", "#publish"]) {
      this.attach(this.onchange, "change", checkbox);
    }
  }

  onchange(e) {
    if (this.find("#restrict").checked) {
      this.secret = generate_key();
    } else {
      this.secret = undefined;
    }
    this.find("#sharingurl").update(this.sharingurl());
  }

  sharingurl() {
    if (this.find("#private").checked) {
      return "";
    } else if (this.find("#restrict").checked) {
      return this.url + "#k=" + this.secret;
    } else {
      return this.url;
    }
  }

  submit() {
    let share = !this.find("#private").checked;
    let publish = this.find("#publish").checked;
    let secret = this.secret;
    this.close({share, publish, secret});
  }

  render() {
    return `
      <md-dialog-top>Share case</md-dialog-top>
      <div id="content">
        <md-radio-button
          id="private"
          name="sharing"
          value="0"
          label="Private (only stored on local computer)">
        </md-radio-button>
        <md-radio-button
          id="share"
          name="sharing"
          value="1"
          label="Share (public so other users can view it)">
        </md-radio-button>
        <md-radio-button
          id="restrict"
          name="sharing"
          value="2"
          label="Restrict (only users with the secret key can view it)">
        </md-radio-button>
        <md-radio-button
          id="publish"
          name="sharing"
          value="3"
          label="Publish (case topics in public knowledge base)">
        </md-radio-button>
        <div>
          Sharing URL:
          <md-copyable-text id="sharingurl"></md-copyable-text>
        </div>
      </div>
      <md-dialog-bottom>
        <button id="cancel">Cancel</button>
        <button id="submit">Share</button>
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
      $ #sharingurl {
        width: 400px;
      }
    `;
  }
}

Component.register(SharingDialog);

var user_script;
const AsyncFunction = Object.getPrototypeOf(async function(){}).constructor;

class ScriptDialog extends MdDialog {
  onconnected() {
    this.bind("textarea", "keydown", e => e.stopPropagation());
  }

  submit() {
    user_script = this.find("textarea").value;
    var func;
    try {
      func = new AsyncFunction("store", "frame", "print", user_script);
    } catch(e) {
      this.find("#msg").innerText = e.message;
      return;
    }
    this.close(func);
  }

  render() {
    return `
      <md-dialog-top>Execute script</md-dialog-top>
      <div id="content">
        <textarea
          rows="32"
          cols="80"
          spellcheck="false">${Component.escape(user_script)}</textarea>
          <div id="msg"></div>
      </div>
      <md-dialog-bottom>
        <button id="cancel">Cancel</button>
        <button id="submit">Run</button>
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
    `;
  }
}

Component.register(ScriptDialog);

class UploadDialog extends MdDialog {
  submit() {
    this.close(this.find("#files").files);
  }

  render() {
    return `
      <md-dialog-top>Upload files</md-dialog-top>
      <div id="content">
        <div>Select files to add to case:</div>
        <input id="files" type="file" multiple>
        <div>Each file will be added as a case topic.</div>
      </div>
      <md-dialog-bottom>
        <button id="cancel">Cancel</button>
        <button id="submit">Upload</button>
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
    `;
  }
}

Component.register(UploadDialog);

export class CollaborateDialog extends MdDialog {
  submit() {
    this.close(this.find("#url").value.trim());
  }

  render() {
    let p = this.state;
    return `
      <md-dialog-top>Collaborate on case</md-dialog-top>
      <div id="content">
        <div>
          You can move the case from your local machine to a collaboration
          server to collaborate with other participants on the case
          in real-time.
        </div>
        <md-text-field
          id="url"
          value="${Component.escape(this.state)}"
          label="Collaboration server URL">
        </md-text-field>
      </div>
      <md-dialog-bottom>
        <button id="cancel">Cancel</button>
        <button id="submit">Collaborate</button>
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
      $ {
        width: 500px;
      }
    `;
  }
}

Component.register(CollaborateDialog);

export class InviteDialog extends MdDialog {
  onconnected() {
    this.attach(this.onitem, "item", "md-search");

    let omnibox = this.find("#value");
    let editor = document.getElementById("editor");
    omnibox.add(editor.search.bind(editor));
    omnibox.add(kbsearch);
    omnibox.add((query, results, options) => {
      results.push({
        ref: query,
        name: query,
        description: "new participant",
        context: new plugins.Context(null, editor.casefile, editor),
        onitem: async item => {
          // Create new topic stub.
          let topic = await item.context.new_topic();
          if (!topic) return;
          topic.put(n_name, item.name.trim());
          return topic;
        },
      });
    });
  }

  async onitem(e) {
    let item = e.detail;

    if (item.onitem) {
      let topic = await item.onitem(item);
      if (item.context) await item.context.refresh();
      this.participant = topic.id;
    } else if (item.topic) {
      this.participant = item.topic.id;
    } else if (item.ref) {
      this.participant = item.ref;
    }

    this.find("md-search").set(item.name);
  }

  submit() {
    this.close(this.participant);
  }

  render() {
    let editor = document.getElementById("editor");
    let userid = editor.localcase.get(n_userid);
    let collab = editor.collab;
    let status;
    if (collab.connected()) {
      status = '<span class="green">⬤</span> Connected';
    } else {
      status = '<span class="red">⬤</span> Disconnected';
    }
    return `
      <md-dialog-top>Invite participant</md-dialog-top>
      <div id="content">
        <div>
          Select participant to invite to collaborate on case.
          You can either choose an existing topic or create a new topic for
          the participant.
        </div>
        <div id="search">
          <omni-box id="value"></omni-box>
        </div>
        <div id="info">
          Server: ${collab.url}<br/>
          Status: ${status}<br/>
          Case #: ${editor.caseid()}<br/>
          Created: ${editor.casefile.get(n_modified)}<br/>
          Your participant id: ${userid}<br/>
        </div>
      </div>
      <md-dialog-bottom>
        <button id="cancel">Cancel</button>
        <button id="submit">Invite</button>
      </md-dialog-bottom>
    `;
  }

  static stylesheet() {
    return `
      $ {
        width: 500px;
      }
      $ #content {
        display: flex;
        flex-direction: column;
        row-gap: 16px;
        height: 300px;
      }
      $ #search {
        height: 100%;
      }
      $ omni-box {
        border: 1px solid #d0d0d0;
        padding: 0px;
      }
      $ omni-box md-search {
        margin: 0px;
      }
      $ md-search-list {
        max-height: 200px;
      }
      $ #info {
        border: 1px solid black;
        padding: 8px;
      }
      $ .red {
        color: red;
      }
      $ .green {
        color: green;
      }
    `;
  }
}

Component.register(InviteDialog);

MdIcon.custom("wikidata", `
<svg width="24" height="24" viewBox="0 0 1050 590" style="fill:white;">
  <path d="m 120,545 h 30 V 45 H 120 V 545 z m 60,0 h 90 V 45 H 180 V 545 z
           M 300,45 V 545 h 90 V 45 h -90 z"/>
  <path d="m 840,545 h 30 V 45 H 840 V 545 z M 900,45 V 545 h 30 V 45 H 900 z
           M 420,545 h 30 V 45 H 420 V 545 z M 480,45 V 545 h 30 V 45 h -30 z"/>
  <path d="m 540,545 h 90 V 45 h -90 V 545 z m 120,0 h 30 V 45 H 660 V 545 z
           M 720,45 V 545 h 90 V 45 H 720 z"/>
</svg>
`);


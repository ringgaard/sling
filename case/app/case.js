// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import * as material from "/common/lib/material.js";
import {PhotoGallery, imageurl, use_mediadb} from "/common/lib/gallery.js";
import {Frame, QString, Encoder} from "/common/lib/frame.js";
import {store, settings} from "./global.js";
import {get_schema} from "./schema.js";

use_mediadb(false);

const n_id = store.id;
const n_is = store.is;
const n_name = store.lookup("name");
const n_alias = store.lookup("alias");
const n_description = store.lookup("description");
const n_caseno = store.lookup("caseno");
const n_main = store.lookup("main");
const n_topics = store.lookup("topics");
const n_folders = store.lookup("folders");
const n_next = store.lookup("next");
const n_publish = store.lookup("publish");
const n_share = store.lookup("share");
const n_link = store.lookup("link");
const n_sling_case_no = store.lookup("PCASE");

const n_target = store.lookup("target");
const n_media = store.lookup("media");

const n_item_type = store.lookup("/w/item");
const n_lexeme_type = store.lookup("/w/lexeme");
const n_string_type = store.lookup("/w/string");
const n_xref_type = store.lookup("/w/xref");
const n_time_type = store.lookup("/w/time");
const n_url_type = store.lookup("/w/url");
const n_media_type = store.lookup("/w/media");
const n_quantity_type = store.lookup("/w/quantity");
const n_geo_type = store.lookup("/w/geo");

const n_amount = store.lookup("/w/amount");
const n_unit = store.lookup("/w/unit");
const n_lat = store.lookup("/w/lat");
const n_lng = store.lookup("/w/lng");
const n_formatter_url = store.lookup("P1630");
const n_media_legend = store.lookup("P2096");
const n_has_quality = store.lookup("P1552");
const n_not_safe_for_work = store.lookup("Q2716583");

// Granularity for time.
const MILLENNIUM = 1;
const CENTURY = 2;
const DECADE = 3
const YEAR = 4;
const MONTH = 5
const DAY = 6;

const month_names = [
  "January", "February", "March",
  "April", "May", "June",
  "July", "August", "September",
  "October", "November", "December",
];

class Time {
  constructor(t) {
    if (typeof(t) === "number") {
      if (t >= 1000000) {
        // YYYYMMDD
        this.year = Math.floor(t / 10000);
        this.month = Math.floor((t % 10000) / 100);
        this.day = Math.floor(t % 100);
        this.precision = DAY;
      } else if (t >= 10000) {
        // YYYYMM
        this.year = Math.floor(t / 100);
        this.month = Math.floor(t % 100);
        this.precision = MONTH;
      } else if (t >= 1000) {
        // YYYY
        this.year = Math.floor(t);
        this.precision = YEAR;
      } else if (t >= 100) {
        // YYY*
        this.year = Math.floor(t * 10);
        this.precision = DECADE;
      } else if (t >= 10) {
        // YY**
        this.year = Math.floor(t * 100 + 1);
        this.precision = CENTURY;
      } else if (t >= 0) {
        // Y***
        this.year = Math.floor(t * 1000 + 1);
        this.precision = MILLENNIUM;
      }
    }
  }

  text() {
    switch (this.precision) {
      case MILLENNIUM:
        if (this.year > 0) {
          let millennium = Math.floor((this.year - 1) / 1000 + 1);
          return millennium + ". millennium AD";
        } else {
          let millennium = Math.floor(-((this.year + 1) / 100 - 1));
          return millennium + ". millennium BC";
        }

      case CENTURY:
        if (this.year > 0) {
          let century = Math.floor((this.year - 1) / 100 + 1);
          return century + ". century AD";
        } else {
          let century = Math.floor(-((this.year + 1) / 100 - 1));
          return century + ". century BC";
        }

      case DECADE:
        return this.year + "s";

      case YEAR:
        return this.year.toString();

      case MONTH:
        return month_names[this.month - 1] + " " + this.year;

      case DAY:
        return month_names[this.month - 1] + " " + this.day + ", " + this.year;

      default:
        return "???";
    }
  }
}

class LabelCollector {
  constructor(store) {
    this.store = store;
    this.items = new Set();
  }

  add(item) {
    // Add all missing values to collector.
    for (let [name, value] of item) {
      if (value instanceof Frame) {
        if (value.isanonymous()) {
          this.add(value);
        } else if (value.isproxy()) {
          this.items.add(value);
        }
      } else if (value instanceof QString) {
        if (value.qual) this.items.add(value.qual);
      }
    }
  }

  async retrieve() {
    // Skip if all labels has already been resolved.
    if (this.items.size == 0) return null;

    // Retrieve stubs from knowledge service.
    let response = await fetch(settings.kbservice + "/kb/stubs", {
      method: 'POST',
      headers: {
        'Content-Type': 'application/sling',
      },
      body: this.store.encode(Array.from(this.items)),
    });
    let stubs = await this.store.parse(response);

    // Mark as stubs.
    for (let stub of stubs) {
      if (stub) stub.markstub();
    }

    return stubs;
  }
};

//-----------------------------------------------------------------------------
// Case Editor
//-----------------------------------------------------------------------------

class CaseEditor extends Component {
  onconnected() {
    this.app = this.match("#app");
    this.bind("#menu", "click", e => this.onmenu(e));
    this.bind("#home", "click", e => this.close());
    this.bind("#save", "click", e => this.onsave(e));
    this.bind("#share", "click", e => this.onshare(e));
    this.bind("#newfolder", "click", e => this.onnewfolder(e));

    document.addEventListener("keydown", e => this.onkeydown(e));
    window.addEventListener("beforeunload", e => this.onbeforeunload(e));
  }

  onbeforeunload(e) {
    // Notify about unsaved changes.
    if (this.dirty) {
      e.preventDefault();
      e.returnValue = "";
    }
  }

  onkeydown(e) {
    if (e.ctrlKey && e.key === 's') {
      e.preventDefault();
      this.onsave(e);
    } else if (e.key === "Escape") {
      this.find("#search").clear();
    }
  }

  async onnewfolder(e) {
    if (this.readonly) return;
    let dialog = new NewFolderDialog();
    let result = await dialog.show();
    if (result) {
      this.add_folder(result);
    }
  }

  onmenu(e) {
    this.find("md-drawer").toogle();
  }

  onsave(e) {
    if (this.readonly) return;
    if (this.dirty) {
      this.match("#app").save_case(this.casefile);
      this.mark_clean();
    }
  }

  async onshare(e) {
    if (this.readonly) return;
    let share = this.casefile.get(n_share);
    let publish = this.casefile.get(n_publish);
    let dialog = new SharingDialog({share, publish});
    let result = await dialog.show();
    if (result && result.share) {
      // Update sharing information.
      if (result.share != share) {
        this.casefile.set(n_share, result.share);
        this.dirty = true;
      }
      if (result.publish != publish) {
        this.casefile.set(n_publish, result.publish);
        this.dirty = true;
      }

      // Make sure the case is saved.
      if (this.dirty) {
        this.match("#app").save_case(this.casefile);
        this.mark_clean();
      }

      // Send case to server.
      let r = await fetch("/case/share", {
        method: 'POST',
        headers: {
          'Content-Type': 'application/sling'
        },
        body: this.encoded()
      });
      if (!r.ok) {
        console.log("Sharing error", r);
        material.StdDialog.error(
          `Error ${r.status} sharing case: ${r.statusText}`);
      }
    }
  }

  onupdate() {
    if (!this.state) return;
    this.mark_clean();

    this.casefile = this.state;
    this.main = this.casefile.get(n_main);
    this.topics = this.casefile.get(n_topics);
    this.folders = this.casefile.get(n_folders);
    this.folder = this.casefile.get(n_folders).value(0);
    this.readonly = this.casefile.get(n_link);

    for (let e of ["#save", "#share", "#newfolder"]) {
      this.find(e).update(!this.readonly);
    }
  }

  onupdated() {
    this.find("#caseno").update(this.caseno().toString());
    this.find("folder-list").update(this.folders);
    this.find("topic-list").update(this.folder);
    this.find("md-drawer").update(true);
  }

  caseno() {
    return this.casefile.get(n_caseno);
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

  next_topic() {
    let next = this.casefile.get(n_next);
    this.casefile.set(n_next, next + 1);
    return next;
  }

  mark_clean() {
    this.dirty = false;
    this.find("#save").disable();
  }

  mark_dirty() {
    this.dirty = true;
    this.find("#save").enable();
  }

  close() {
    if (this.dirty) {
      let msg = `Changes to case #${this.caseno()} has not been saved.`;
      let buttons = {
        "Close without saving": "close",
        "Cancel": "cancel",
        "Save": "save",
      }
      material.StdDialog.choose("Discard changes?", msg, buttons)
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

  folderno(folder) {
    let f = this.folders;
    for (let i = 0; i < f.length; ++i) {
      if (f.value(i) == folder) return i;
    }
    return undefined;
  }

  show_folder(folder) {
    if (folder != this.folder) {
      this.folder = folder;
      this.find("folder-list").update(this.folders);
      this.find("topic-list").update(this.folder);
    }
  }

  add_folder(name) {
    if (this.readonly) return;
    this.folder = new Array();
    this.folders.add(name, this.folder);
    this.find("folder-list").update(this.folders);
    this.find("topic-list").update(this.folder);
    this.mark_dirty();
  }

  rename_folder(folder, name) {
    if (this.readonly) return;
    let pos = this.folderno(folder);
    if (pos > 0 && pos < this.folders.length) {
      this.folders.set_name(pos, name);
      this.find("folder-list").update(this.folders);
      this.mark_dirty();
    }
  }

  move_folder_up(folder) {
    if (this.readonly) return;
    let pos = this.folderno(folder);
    if (pos > 1 && pos < this.folders.length) {
      // Swap with previous folder.
      let tmp_name = this.folders.name(pos);
      let tmp_value = this.folders.value(pos);
      this.folders.set_name(pos, this.folders.name(pos - 1));
      this.folders.set_value(pos, this.folders.value(pos - 1));
      this.folders.set_name(pos - 1, tmp_name);
      this.folders.set_value(pos - 1, tmp_value);
      this.mark_dirty();
      this.find("folder-list").update(this.folders);
    }
  }

  move_folder_down(folder) {
    if (this.readonly) return;
    let pos = this.folderno(folder);
    if (pos > 0 && pos < this.folders.length - 1) {
      // Swap with next folder.
      let tmp_name = this.folders.name(pos);
      let tmp_value = this.folders.value(pos);
      this.folders.set_name(pos, this.folders.name(pos + 1));
      this.folders.set_value(pos, this.folders.value(pos + 1));
      this.folders.set_name(pos + 1, tmp_name);
      this.folders.set_value(pos + 1, tmp_value);
      this.mark_dirty();
      this.find("folder-list").update(this.folders);
    }
  }

  delete_folder(folder) {
    if (this.readonly) return;
    if (folder.length != 0) {
      material.StdDialog.alert("Cannot delete folder",
                      "Folder must be empty to be deleted");
    } else {
      let pos = this.folderno(folder);
      if (pos >= 0 && pos < this.folders.length) {
        this.folders.remove(pos);
        if (pos == this.folders.length) {
          this.folder = this.folders.value(pos - 1);
        } else {
          this.folder = this.folders.value(pos);
        }
        this.find("folder-list").update(this.folders);
        this.find("topic-list").update(this.folder);
        this.mark_dirty();
      }
    }
  }

  add_topic(itemid, name) {
    if (this.readonly) return;

    // Create new topic.
    let topicid = this.next_topic();
    let topic = store.frame(`t/${this.caseno()}/${topicid}`);
    if (itemid) topic.add(n_is, store.lookup(itemid));
    if (name) topic.add(n_name, name);
    this.mark_dirty();

    // Add topic to current folder.
    this.topics.push(topic);
    this.folder.push(topic);

    // Update topic list.
    let topic_list = this.find("topic-list");
    topic_list.update(this.folder);
    topic_list.scroll_to(topic);
  }

  delete_topic(topic) {
    if (this.readonly) return;

    // Do not delete main topic.
    if (topic == this.casefile.get(n_main)) return;

    // Delete topic from case and current folder.
    this.topics.splice(this.topics.indexOf(topic), 1);
    this.folder.splice(this.folder.indexOf(topic), 1);
    this.mark_dirty();

    // Update topic list.
    this.find("topic-list").update(this.folder);
  }

  move_topic_up(topic) {
    if (this.readonly) return;
    let pos = this.folder.indexOf(topic);
    if (pos == -1) return;
    if (pos == 0 || this.folder.length == 1) return;

    // Swap with previous topic.
    let tmp = this.folder[pos];
    this.folder[pos] = this.folder[pos - 1];
    this.folder[pos - 1] = tmp;
    this.mark_dirty();

    // Update topic list.
    this.find("topic-list").update(this.folder);
  }

  move_topic_down(topic) {
    if (this.readonly) return;
    let pos = this.folder.indexOf(topic);
    if (pos == -1) return;
    if (pos == this.folder.length - 1 || this.folder.length == 1) return;

    // Swap with next topic.
    let tmp = this.folder[pos];
    this.folder[pos] = this.folder[pos + 1];
    this.folder[pos + 1] = tmp;
    this.mark_dirty();

    // Update topic list.
    this.find("topic-list").update(this.folder);
  }

  prerender() {
    return `
      <md-column-layout>
        <md-toolbar>
          <md-icon-button id="menu" icon="menu"></md-icon-button>
          <md-toolbar-logo></md-toolbar-logo>
          <div id="title">Case #<md-text id="caseno"></md-text></div>
          <topic-search-box id="search"></topic-search-box>
          <md-spacer></md-spacer>
          <md-icon-button id="save" icon="save"></md-icon-button>
          <md-icon-button id="share" icon="share"></md-icon-button>
        </md-toolbar>

        <md-row-layout>
          <md-drawer>
            <div id="home">
              <md-icon-button icon="home"></md-icon-button>
              SLING Cases Home
            </div>
            <div id="folders-top">
              Folders
              <md-spacer></md-spacer>
              <md-icon-button id="newfolder" icon="create_new_folder">
              </md-icon-button>
            </div>
            <folder-list></folder-list>
          </md-drawer>
          <md-content>
            <topic-list></topic-list>
          </md-content>
        </md-row-layout>
      </md-column-layout>
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
      $ md-row-layout {
        overflow: auto;
        height: 100%;
      }
      $ md-drawer md-icon {
        color: #808080;
      }
      $ md-drawer md-icon-button {
        color: #808080;
      }
      $ topic-list md-icon {
        color: #808080;
      }
      $ topic-list md-icon-button {
        color: #808080;
        fill: #808080;
      }
      $ md-drawer {
        padding: 3px;
        box-sizing: border-box;
      }
      $ #home {
        display: flex;
        align-items: center;
        padding-right: 16px;
        font-weight: bold;
        white-space: nowrap;
        cursor: pointer;
      }
      $ #folders-top {
        display: flex;
        align-items: center;
        font-size: 16px;
        font-weight: bold;
        margin-left: 6px;
        border-bottom: thin solid #808080;
        margin-bottom: 6px;
      }
    `;
  }
}

Component.register(CaseEditor);

class TopicSearchBox extends Component {
  onconnected() {
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
      material.StdDialog.error(error.message);
      target.populate(detail, null);
    });
  }

  onenter(e) {
    let name = e.detail;
    this.match("#editor").add_topic(null, name);
  }

  onitem(e) {
    let item = e.detail;
    this.match("#editor").add_topic(item.ref, item.name);
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
          placeholder="Search for topic..."
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
    `;
  }
}

Component.register(TopicSearchBox);

class FolderList extends Component {
  render() {
    let folders = this.state;
    if (!folders) return;
    let editor = this.match("#editor");
    if (!editor) return;
    let readonly = editor.readonly;
    let current = editor.folder;
    let h = [];
    for (let [name, folder] of folders) {
      let marked = folder == current;
      h.push(new CaseFolder({name, folder, readonly, marked}));
    }
    return h;
  }
}

Component.register(FolderList);

class CaseFolder extends Component {
  onconnected() {
    this.bind(null, "click", e => this.onclick(e));
    if (!this.state.readonly) {
      this.bind("#rename", "select", e => this.onrename(e));
      this.bind("#moveup", "select", e => this.onmoveup(e));
      this.bind("#movedown", "select", e => this.onmovedown(e));
      this.bind("#delete", "select", e => this.ondelete(e));
    }
  }

  onclick(e) {
    this.match("#editor").show_folder(this.state.folder);
  }

  async onrename(e) {
    let dialog = new RenameFolderDialog(this.state.name);
    let result = await dialog.show();
    if (result) {
      this.match("#editor").rename_folder(this.state.folder, result);
    }
  }

  onmoveup(e) {
    this.match("#editor").move_folder_up(this.state.folder);
  }

  onmovedown(e) {
    this.match("#editor").move_folder_down(this.state.folder);
  }

  ondelete(e) {
    this.match("#editor").delete_folder(this.state.folder);
  }

  render() {
    let menu = `
      <md-spacer></md-spacer>
      <md-menu>
        <md-menu-item id="rename">
          <md-icon icon="drive_file_rename_outline"></md-icon>Rename
        </md-menu-item>
        <md-menu-item id="moveup">
          <md-icon icon="move-up"></md-icon>Move up
        </md-menu-item>
        <md-menu-item id="movedown">
          <md-icon icon="move-down"></md-icon>Move down
        </md-menu-item>
        <md-menu-item id="delete">
         <md-icon icon="delete"></md-icon>Delete
        </md-menu-item>
      </md-menu>
    `;

    return `
      <md-icon icon="folder"></md-icon>
      <div ${this.state.marked ? 'class="current"' : ''}>
        ${Component.escape(this.state.name)}
      </div>
      ${this.state.readonly ? "" : menu}
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        align-items: center;
        padding-left: 6px;
        white-space: nowrap;
        cursor: pointer;
        height: 30px;
        border-radius: 15px;
        border: 0;
        fill: #808080;
      }
      $ md-icon-button button {
        height: 30px;
        width: 30px;
        border-radius: 15px;
      }
      $:hover {
        background-color: #eeeeee;
      }
      $ md-menu #open {
        visibility: hidden;
      }
      $:hover md-menu #open {
        visibility: visible;
      }
      $:hover {
        background-color: #eeeeee;
      }
      $ div {
        padding-left: 6px;
      }
      $ div.current {
        font-weight: bold;
        padding-left: 6px;
      }
    `;
  }
}

Component.register(CaseFolder);

class NewFolderDialog extends material.MdDialog {
  submit() {
    this.close(this.find("#name").value.trim());
  }

  render() {
    let p = this.state;
    return `
      <md-dialog-top>Create new case folder</md-dialog-top>
      <div id="content">
        <md-text-field
          id="name"
          value="${Component.escape(this.state)}"
          label="Folder name">
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
    `;
  }
}

Component.register(NewFolderDialog);

class RenameFolderDialog extends material.MdDialog {
  submit() {
    this.close(this.find("#name").value.trim());
  }

  render() {
    let p = this.state;
    return `
      <md-dialog-top>Rename case folder</md-dialog-top>
      <div id="content">
        <md-text-field
          id="name"
          value="${Component.escape(this.state)}"
          label="Folder name">
        </md-text-field>
      </div>
      <md-dialog-bottom>
        <button id="cancel">Cancel</button>
        <button id="submit">Rename</button>
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
    `;
  }
}

Component.register(RenameFolderDialog);

class SharingDialog extends material.MdDialog {
  onconnected() {
    this.find("#share").update(this.state.share);
    this.find("#publish").update(this.state.publish);
  }

  submit() {
    this.close({
      share: this.find("#share").checked,
      publish: this.find("#publish").checked,
    });
  }

  render() {
    return `
      <md-dialog-top>Share case</md-dialog-top>
      <div id="content">
        <md-checkbox id="share" label="Share case in the public case store">
        </md-checkbox>
        <md-checkbox id="publish" label="Publish case topics in knowledge base">
        </md-checkbox>
      </div>
      <md-dialog-bottom>
        <button id="cancel">Cancel</button>
        <button id="submit">Share</button>
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
    `;
  }
}

Component.register(SharingDialog);

class TopicList extends Component {
  constructor(state) {
    super(state);
    this.selection = new Set();
  }

  async onupdate() {
    // Retrieve labels for all topics.
    let topics = this.state;
    if (topics) {
      // Wait until schema loaded.
      await get_schema();

      let labels = new LabelCollector(store)
      for (let topic of topics) {
        labels.add(topic);
      }
      await labels.retrieve();
    }

    // Clear selection.
    this.selection.clear();
  }

  clear_selection() {
    for (let topic of this.selection) {
      this.card(topic).unselect();
    }
    this.selection.clear();
  }

  select(topic, extend) {
    if (!extend) this.clear_selection();
    this.selection.add(topic);
    this.card(topic).select();
  }

  unselect(topic) {
    if (this.selection.has(topic)) {
      this.selection.delete(topic);
      this.card(topic).unselect();
    }
  }

  scroll_to(topic) {
    this.card(topic).scrollIntoView();
  }

  card(topic) {
    for (let i = 0; i < this.children.length; i++) {
      let card = this.children[i];
      if (card.state == topic) return card;
    }
    return null;
  }

  render() {
    let topics = this.state;
    if (!topics) return;
    let h = [];
    for (let topic of topics) {
      h.push(new TopicCard(topic));
    }
    return h;
  }

  static stylesheet() {
    return `
    `;
  }
}

Component.register(TopicList);

class TopicCard extends material.MdCard {
  onconnected() {
    let editor = this.match("#editor");
    this.readonly = editor && editor.readonly;
    if (!this.readonly) {
      this.bind("#delete", "click", e => this.ondelete(e));
      this.bind("#moveup", "click", e => this.onmoveup(e));
      this.bind("#movedown", "click", e => this.onmovedown(e));
      this.bind("#edit", "click", e => this.onedit(e));
      this.bind("#save", "click", e => this.onsave(e));
      this.bind("#discard", "click", e => this.ondiscard(e));
    }

    this.bind(null, "click", e => this.onclick(e));
    this.bind(null, "keydown", e => this.onkeydown(e));

    this.update_mode(false);
    this.find("#name").update(this.state.get(n_name));
  }

  onupdated() {
    this.find("#name").update(this.state.get(n_name));
  }

  update_mode(editing) {
    this.editing = editing;

    if (editing) {
      this.find("#mode").update("#edit");
    } else {
      this.find("#mode").update("#view", this.state);
    }

    this.find("#topic-actions").update(!editing && !this.readonly);
    this.find("#edit-actions").update(editing && !this.readonly);
  }

  selected() {
    return this.classList.contains("selected");
  }

  select() {
    this.classList.add("selected");
  }

  unselect() {
    this.classList.remove("selected");
  }

  onedit(e) {
    e.stopPropagation();
    this.update_mode(true);

    let topic = this.state;
    let pre = this.find("pre");
    pre.innerHTML = Component.escape(topic.text(true));
    pre.focus();
  }

  async onsave(e) {
    e.stopPropagation();
    let pre = this.find("pre");
    let content = pre.textContent;
    var topic;
    try {
      topic = store.parse(content);
    } catch (error) {
      console.log("parse error", content);
      material.StdDialog.error(`Error parsing topic: ${error}`);
      return;
    }

    let labels = new LabelCollector(store)
    labels.add(topic);
    await labels.retrieve();

    this.update(topic);
    this.update_mode(false);
    this.match("#editor").mark_dirty();
  }

  ondiscard(e) {
    this.update(this.state);
    this.update_mode(false);
  }

  async ondelete(e) {
    e.stopPropagation();
    let topic = this.state;
    let result = await material.StdDialog.confirm(
      "Delete topic",
      `Delete topic '${topic.get(n_name)}'?`,
      "Delete");
    if (result) {
      this.match("#editor").delete_topic(topic);
    }
  }

  onmoveup(e) {
    e.stopPropagation();
    this.match("#editor").move_topic_up(this.state);
  }

  onmovedown(e) {
    e.stopPropagation();
    this.match("#editor").move_topic_down(this.state);
  }

  onkeydown(e) {
    if (e.key === "s" && e.ctrlKey && this.editing) {
      this.onsave(e);
      e.stopPropagation();
      e.preventDefault();
    } else if (e.key === "Escape" && this.editing) {
      this.ondiscard(e);
      e.stopPropagation();
      e.preventDefault();
    }
  }

  onclick(e) {
    let topic = this.state;
    let list = this.match("topic-list");
    if (e.ctrlKey) {
      if (this.selected()) {
        list.unselect(topic);
      } else {
        list.select(topic, true);
      }
    } else {
      if (this.selected()) {
        list.clear_selection();
      } else {
        list.select(topic, false);
      }
    }
  }

  prerender() {
    let topic = this.state;
    if (!topic) return;

    return `
      <md-card-toolbar>
        <md-text id="name"></md-text>
        <md-spacer></md-spacer>
        <md-toolbox id="edit-actions">
          <md-icon-button id="save" icon="save_alt"></md-icon-button>
          <md-icon-button id="discard" icon="cancel"></md-icon-button>
        </md-toolbox>
        <md-toolbox id="topic-actions">
          <md-icon-button id="edit" icon="edit"></md-icon-button>
          <md-icon-button id="moveup" icon="move-up"></md-icon-button>
          <md-icon-button id="movedown" icon="move-down"></md-icon-button>
          <md-icon-button id="delete" icon="delete"></md-icon-button>
        </md-toolbox>
      </md-card-toolbar>
      <one-of id="mode">
        <item-panel id="view"></item-panel>
        <pre id="edit" spellcheck="false" contenteditable="true"></pre>
      </one-of>
    `;
  }

  static stylesheet() {
    return material.MdCard.stylesheet() + `
      $ {
        margin: 5px 5px 15px 5px;
        border: 1px solid #ffffff;
      }
      $.selected {
        border: 1px solid #d0d0d0;
        box-shadow: rgb(0 0 0 / 16%) 0px 4px 8px 0px,
                    rgb(0 0 0 / 23%) 0px 4px 8px 0px;
      }
      $ md-card-toolbar {
        position: relative;
        margin-bottom: 0px;
      }
      $ md-toolbox {
        top: -6px;
      }
      $ #name {
        display: block;
        font-size: 24px;
      }
      $ pre {
        font-size: 12px;
        padding: 6px;
      }
      $ #edit-actions {
        display: flex;
      }
    `;
  }
}

Component.register(TopicCard);

class KbLink extends Component {
  onconnected() {
    this.bind(null, "click", e => this.onclick(e));
  }

  onclick(e) {
    window.open(`${settings.kbservice}/kb/${this.props.ref}`, "_blank");
  }

  static stylesheet() {
    return `
      $ {
        color: #0b0080;
      }

      $:hover {
        cursor: pointer;
      }
    `;
  }
}

Component.register(KbLink);

// Convert geo coordinate from decimal to degrees, minutes and seconds.
function convert_geo_coord(coord, latitude) {
  // Compute direction.
  var direction;
  if (coord < 0) {
    coord = -coord;
    direction = latitude ? "S" : "W";
  } else {
    direction = latitude ? "N" : "E";
  }

  // Compute degrees.
  let degrees = Math.floor(coord);

  // Compute minutes.
  let minutes = Math.floor(coord * 60) % 60;

  // Compute seconds.
  let seconds = Math.floor(coord * 3600) % 60;

  // Build coordinate string.
  return degrees +  "°" + minutes + "′" + seconds + "″" + direction;
}

class PropertyPanel extends Component {
  onconnected() {
    this.bind(null, "click", e => { e.stopPropagation(); });
  }

  visible() {
    return this.state && this.state.length > 0;
  }

  render() {
    let item = this.state;
    let store = item.store;
    let h = [];

    function render_name(prop) {
      let name = prop.get(n_name);
      if (!name) name = prop.id;
      h.push(`<kb-link ref="${prop.id}">`);
      h.push(Component.escape(name));
      h.push('</kb-link>');
    }

    function render_quantity(val) {
      var amount, unit;
      if (val instanceof Frame) {
        amount = val.get(n_amount);
        unit = val.get(n_unit);
      } else {
        amount = val;
      }

      if (typeof amount === 'number') {
        amount = Math.round(amount * 1000) / 1000;
      }

      h.push(Component.escape(amount));
      if (unit) {
        h.push(" ");
        render_value(unit);
      }
    }

    function render_time(val) {
      let t = new Time(val);
      h.push(t.text());
    }

    function render_link(val) {
      let name = val.get(n_name);
      if (!name) name = val.id;
      if (name) {
        h.push(`<kb-link ref="${val.id}">`);
        h.push(Component.escape(name));
        h.push('</kb-link>');
      } else {
        render_fallback(val);
      }
    }

    function render_text(val) {
      if (val instanceof QString) {
        h.push(Component.escape(val.text));
        if (val.qual) {
          let lang = val.qual.get(n_name);
          if (!lang) lang = val.qual.id;
          h.push(' <span class="prop-lang">[');
          render_value(lang);
          h.push(']</span>');
        }
      } else {
        h.push(Component.escape(val));
      }
    }

    function render_xref(val, prop) {
      let formatter = prop.resolved(n_formatter_url);
      if (formatter) {
        let url = formatter.replace("$1", val);
        h.push('<a href="');
        h.push(url);
        h.push('" target="_blank" rel="noreferrer">');
        render_value(val);
        h.push('</a>');
      } else {
        render_value(val);
      }
    }

    function render_url(val) {
      h.push('<a href="');
      h.push(val);
      h.push('" target="_blank" rel="noreferrer">');
      render_value(val);
      h.push('</a>');
    }

    function render_coord(val) {
      let lat = val.get(n_lat);
      let lng = val.get(n_lng);
      let url = `http://maps.google.com/maps?q=${lat},${lng}`;

      h.push('<a href="');
      h.push(url);
      h.push('" target="_blank" rel="noreferrer">');
      h.push(convert_geo_coord(lat, true));
      h.push(", ");
      h.push(convert_geo_coord(lng, false));
      h.push('</a>');
    }

    function render_fallback(val) {
      if (val instanceof Frame) {
        if (val.isanonymous()) {
          if (val.has(n_amount)) {
            render_quantity(val);
          } else {
            h.push(Component.escape(val.text()));
          }
        } else {
          render_link(val);
        }
      } else {
        render_text(val);
      }
    }

    function render_value(val, prop) {
      let dt = prop ? prop.get(n_target) : undefined;
      switch (dt) {
        case n_item_type:
          if (val instanceof Frame) {
            render_link(val);
          } else {
            render_fallback(val);
          }
          break;
        case n_xref_type:
        case n_media_type:
          render_xref(val, prop);
          break;
        case n_time_type:
          render_time(val);
          break;
        case n_quantity_type:
          render_quantity(val);
          break;
        case n_string_type:
          render_text(val);
          break;
        case n_url_type:
          render_url(val);
          break;
        case n_geo_type:
          render_coord(val);
          break;
        default:
          render_fallback(val);
      }
    }

    let prev = null;
    for (let [name, value] of item) {
      if (name == n_id) continue;
      if (name.isproxy()) continue;

      if (name != prev) {
        // Start new property group for new property.
        if (prev != null) h.push('</div></div>');
        h.push('<div class="prop-row">');

        // Property name.
        h.push('<div class="prop-name">');
        render_name(name);
        h.push('</div>');
        h.push('<div class="prop-values">');
        prev = name;
      }

      // Property value.
      let v = store.resolve(value);
      h.push('<div class="prop-value">');
      render_value(v, name);
      h.push('</div>');

      // Qualifiers.
      if (v != value) {
        h.push('<div class="qual-tab">');
        let qprev = null;
        for (let [qname, qvalue] of value) {
          if (qname == n_is) continue;
          if (qname.isproxy()) continue;

          if (qname != qprev) {
            // Start new property group for new property.
            if (qprev != null) h.push('</div></div>');
            h.push('<div class="qual-row">');

            // Qualified property name.
            h.push('<div class="qprop-name">');
            render_name(qname);
            h.push('</div>');
            h.push('<div class="qprop-values">');
            qprev = qname;
          }

          // Qualified property value.
          h.push('<div class="qprop-value">');
          render_value(qvalue, qname);
          h.push('</div>');
        }
        if (qprev != null) h.push('</div></div>');
        h.push('</div>');
      }
    }
    if (prev != null) h.push('</div></div>');

    return h.join("");
  }

  static stylesheet() {
    return `
      $ {
        display: table;
        font-size: 16px;
        border-collapse: collapse;
        width: 100%;
        table-layout: fixed;
      }

      $ .prop-row {
        display: table-row;
        border-top: thin solid lightgrey;
      }

      $ .prop-row:first-child {
        display: table-row;
        border-top: none;
      }

      $ .prop-name {
        display: table-cell;
        font-weight: 500;
        width: 20%;
        padding: 8px;
        vertical-align: top;
        overflow-wrap: break-word;
      }

      $ .prop-values {
        display: table-cell;
        vertical-align: top;
        padding-bottom: 8px;
      }

      $ .prop-value {
        padding: 8px 8px 0px 8px;
        overflow-wrap: break-word;
      }

      $ .prop-lang {
        color: #808080;
        font-size: 13px;
      }

      $ .prop-value a {
        color: #0b0080;
        text-decoration: none;
        cursor: pointer;
        outline: none;
      }

      $ .qual-tab {
        display: table;
        border-collapse: collapse;
      }

      $ .qual-row {
        display: table-row;
      }

      $ .qprop-name {
        display: table-cell;
        font-size: 13px;
        vertical-align: top;
        padding: 1px 3px 1px 30px;
        width: 150px;
      }

      $ .qprop-values {
        display: table-cell;
        vertical-align: top;
      }

      $ .qprop-value {
        font-size: 13px;
        vertical-align: top;
        padding: 1px;
      }

      $ .qprop-value a {
        color: #0b0080;
        text-decoration: none;
        cursor: pointer;
        outline: none;
      }
    `;
  }
}

Component.register(PropertyPanel);

class XrefPanel extends PropertyPanel {
  static stylesheet() {
    return PropertyPanel.stylesheet() + `
      $ .prop-name {
        font-size: 13px;
        font-weight: normal;
        width: 40%;
        padding: 8px;
        vertical-align: top;
      }

      $ .prop-values {
        font-size: 13px;
        max-width: 0;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
      }

      $ .qprop-name {
        font-size: 11px;
        vertical-align: top;
        padding: 1px 3px 1px 20px;
        width: 100px;
      }

      $ .qprop-value {
        font-size: 11px;
        padding: 1px;
      }
    `;
  }
}

Component.register(XrefPanel);

class PicturePanel extends Component {
  onconnected() {
    this.bind(null, "click", e => this.onopen(e));
  }

  onupdated() {
    let images = this.state;
    if (images && images.length > 0) {
      let index = 0;
      for (let i = 0; i < images.length; ++i) {
        if (!images[i].nsfw) {
          index = i;
          break;
        }
      }
      let image = images[index];
      let caption = image.text;
      if (caption) {
        caption = caption.replace(/\[\[|\]\]/g, '');
      }
      if (images.length > 1) {
        if (!caption) caption = "";
        caption += ` [${index + 1}/${images.length}]`;
      }

      this.find(".photo").update(imageurl(image.url, true));
      this.find(".caption").update(caption);
    } else {
      this.find(".photo").update(null);
      this.find(".caption").update(null);
    }
  }

  visible() {
    return this.state && this.state.length > 0;
  }

  onopen(e) {
    let modal = new PhotoGallery();
    modal.open(this.state);
  }

  static stylesheet() {
    return `
      $ {
        text-align: center;
        cursor: pointer;
      }

      $ .caption {
        display: block;
        font-size: 13px;
        color: #808080;
        padding: 5px;
      }

      $ img {
        max-width: 100%;
        max-height: 320px;
        vertical-align: middle
      }
    `;
  }
}

Component.register(PicturePanel);

class TopicExpander extends Component {
  onconnected() {
    this.bind(null, "click", e => this.onclick(e));
  }

  onclick(e) {
    if (this.expansion) {
      this.close();
    } else {
      this.open();
    }
  }

  async open() {
    let item = this.state;

    // Retrieve item if needed.
    if (!item.ispublic()) {
      let url = `${settings.kbservice}/kb/topic?id=${item.id}`;
      let response = await fetch(url);
      item = await store.parse(response);
    }

    // Retrieve labels.
    let labels = new LabelCollector(store)
    labels.add(item);
    await labels.retrieve();

    // Add item panel for subtopic.
    let panel = this.match("subtopic-panel");
    this.expansion = new ItemPanel(item);
    panel.appendChild(this.expansion);
    this.update(this.state);
  }

  close() {
    // Add item panel for subtopic.
    let panel = this.match("subtopic-panel");
    panel.removeChild(this.expansion);
    this.expansion = undefined;
    this.update(this.state);
  }

  render() {
    let topic = this.state;
    if (!topic) return;
    return `
      <div>${Component.escape(topic.id)}</div>
      <md-icon icon="${this.expansion ? "expand_less" : "expand_more"}">
      </md-icon>
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        align-items: center;
        cursor: pointer;
        font-size: 13px;
        color: #808080;
      }
    `;
  }
}

Component.register(TopicExpander);

class TopicBar extends Component {
  visible() {
    return this.state && this.state.length > 0;
  }

  render() {
    if (!this.state) return;
    let h = new Array();
    for (let subtopic of this.state) {
      h.push(new TopicExpander(subtopic));
    }
    return h;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        justify-content: space-evenly;
        flex-direction: row;
      }
    `;
  }
}

Component.register(TopicBar);

class SubtopicPanel extends Component {
  visible() {
    return this.state && this.state.length > 0;
  }

  render() {
    return new TopicBar(this.state);
  }
}

Component.register(SubtopicPanel);

class ItemPanel extends Component {
  onconnected() {
    if (this.state) this.onupdated();
  }

  onupdated() {
    // Split item into properties, media, xrefs, and subtopics.
    let item = this.state;
    if (!item) return;
    let top = this.parentNode.closest("item-panel") == null;
    let names = new Array();
    let title = item.get(n_name);
    let props = new Frame(store);
    let xrefs = new Frame(store);
    let gallery = [];
    let subtopics = new Array();
    for (let [name, value] of item) {
      if (name === n_media) {
        if (value instanceof Frame) {
          gallery.push({
            url: store.resolve(value),
            caption: value.get(n_media_legend),
            nsfw: value.get(n_has_quality) == n_not_safe_for_work,
          });
        } else {
          gallery.push({url: value});
        }
      } else if (name === n_is) {
        subtopics.push(value);
      } else if (name === n_name || name === n_alias) {
        let n = store.resolve(value).toString();
        if (!top || n != title) names.push(n);
      } else if ((name instanceof Frame) && name.get(n_target) == n_xref_type) {
        xrefs.add(name, value);
      } else {
        props.add(name, value);
      }
    }

    // Update panels.
    this.find("#identifier").update(item.id);
    this.find("#names").update(names.join(" • "));
    this.find("#description").update(item.get(n_description));
    this.find("#properties").update(props);
    this.find("#picture").update(gallery);
    this.find("#xrefs").update(xrefs);
    this.find("#subtopics").update(subtopics);

    // Update rulers.
    if (props.length == 0 || (gallery.length == 0 && xrefs.length == 0)) {
      this.find("#vruler").style.display = "none";
    } else {
      this.find("#vruler").style.display = "";
    }
    if (gallery.length == 0 || xrefs.length == 0) {
      this.find("#hruler").style.display = "none";
    } else {
      this.find("#hruler").style.display = "";
    }
  }

  visible() {
    return this.state && this.state.length > 0;
  }

  render() {
    return `
      <div>
        <md-text id="identifier"></md-text>:
        <md-text id="names"></md-text>
      </div>
      <div><md-text id="description"></md-text></div>
      <md-row-layout>
        <md-column-layout style="flex: 1 1 66%;">
          <property-panel id="properties">
          </property-panel>
        </md-column-layout>

        <div id="vruler"></div>

        <md-column-layout style="flex: 1 1 33%;">
          <picture-panel id="picture">
            <md-image class="photo"></md-image>
            <md-text class="caption"></md-text>
          </picture-panel>
          <div id="hruler"></div>
          <xref-panel id="xrefs">
          </xref-panel>
        </md-column-layout>
      </md-row-layout>
      <subtopic-panel id="subtopics"></subtopic-panel>
    `;
  }

  static stylesheet() {
    return `
      $ #identifier {
        font-size: 13px;
        color: #808080;
      }

      $ #names {
        font-size: 13px;
        color: #808080;
      }

      $ #description {
        font-size: 16px;
      }

      $ #vruler {
        background-color: lightgrey;
        width: 1px;
        max-width: 1px;
        margin-left: 10px;
        margin-right: 10px;
      }

      $ #hruler {
        background-color: lightgrey;
        height: 1px;
        max-height: 1px;
        margin-top: 10px;
        margin-bottom: 10px;
      }
    `;
  }
};

Component.register(ItemPanel);

material.MdIcon.custom("move-down", `
<svg width="24" height="24" viewBox="0 0 32 32">
  <g><polygon points="30,16 22,16 22,2 10,2 10,16 2,16 16,30"/></g>
</svg>
`);

material.MdIcon.custom("move-up", `
<svg width="24" height="24" viewBox="0 0 32 32">
  <g><polygon points="30,14 22,14 22,28 10,28 10,14 2,14 16,0"/></g>
</svg>
`);


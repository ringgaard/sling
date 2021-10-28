// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import * as material from "/common/lib/material.js";
import {Frame, QString, Encoder} from "/common/lib/frame.js";
import {store, settings} from "./global.js";
import {get_schema} from "./schema.js";
import {LabelCollector} from "./item.js"

const n_id = store.id;
const n_is = store.is;
const n_name = store.lookup("name");
const n_alias = store.lookup("alias");
const n_description = store.lookup("description");
const n_caseid = store.lookup("caseid");
const n_main = store.lookup("main");
const n_topics = store.lookup("topics");
const n_folders = store.lookup("folders");
const n_next = store.lookup("next");
const n_publish = store.lookup("publish");
const n_share = store.lookup("share");
const n_link = store.lookup("link");
const n_modified = store.lookup("modified");
const n_shared = store.lookup("shared");
const n_sling_case_no = store.lookup("PCASE");
const n_target = store.lookup("target");
const n_item_type = store.lookup("/w/item");

//-----------------------------------------------------------------------------
// Case Editor
//-----------------------------------------------------------------------------

class CaseEditor extends Component {
  onconnected() {
    this.bind("md-search", "item", e => this.onitem(e));
    this.bind("md-search", "enter", e => this.onenter(e));

    this.bind("#menu", "click", e => this.onmenu(e));
    this.bind("#home", "click", e => this.close());
    this.bind("#save", "click", e => this.onsave(e));
    this.bind("#share", "click", e => this.onshare(e));
    this.bind("#newfolder", "click", e => this.onnewfolder(e));

    this.bind(null, "navigate", e => this.onnavigate(e));

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

  onnavigate(e) {
    let ref = e.detail;
    let item = store.find(ref);
    if (item && this.topics.includes(item)) {
      this.navigate_to(item);
    } else {
      window.open(`${settings.kbservice}/kb/${ref}`, "_blank");
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

  onenter(e) {
    let name = e.detail;
    this.add_topic(null, name);
  }

  onitem(e) {
    let item = e.detail;
    if (item.topic) {
      this.navigate_to(item.topic);
    } else {
      this.add_topic(item.ref, item.name);
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
      // Update modification time.
      let ts = new Date().toJSON();
      this.casefile.set(n_modified, ts);

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
    if (result) {
      // Update sharing information.
      this.casefile.set(n_share, result.share);
      this.casefile.set(n_publish, result.publish);

      // Update modification and sharing time.
      let ts = new Date().toJSON();
      this.casefile.set(n_modified, ts);
      if (result.share) {
        this.casefile.set(n_shared, ts);
      } else {
        this.casefile.set(n_shared, null);
      }

      // Save case before sharing.
      this.match("#app").save_case(this.casefile);
      this.mark_clean();

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
    this.find("#caseid").update(this.caseid().toString());
    this.find("md-drawer").update(true);
    this.update_folders();
    this.update_topics();
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
      let msg = `Changes to case #${this.caseid()} has not been saved.`;
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

  search(query, full) {
    query = query.toLowerCase();
    let results = [];
    let partial = [];
    for (let topic of this.topics) {
      let match = false;
      let submatch = false;
      if (topic.id == query) {
        match = true;
      } else {
        let names = [];
        names.push(...topic.all(n_name));
        names.push(...topic.all(n_alias));
        for (let name of names) {
          let normalized = name.toString().toLowerCase();
          if (full) {
            match = normalized == query;
          } else {
            match = normalized.startsWith(query);
            if (!match && normalized.includes(query)) {
              submatch = true;
            }
          }
          if (match) break;
        }
      }
      if (match) {
        results.push(topic);
      } else if (submatch) {
        partial.push(topic);
      }
    }
    results.push(...partial);
    return results;
  }

  folderno(folder) {
    let f = this.folders;
    for (let i = 0; i < f.length; ++i) {
      if (f.value(i) == folder) return i;
    }
    return undefined;
  }

  async show_folder(folder) {
    if (folder != this.folder) {
      this.folder = folder;
      await this.update_folders();
      await this.update_topics();
    }
  }

  add_folder(name) {
    if (this.readonly) return;
    this.folder = new Array();
    this.folders.add(name, this.folder);
    this.mark_dirty();
    this.update_folders();
    this.update_topics();
  }

  rename_folder(folder, name) {
    if (this.readonly) return;
    let pos = this.folderno(folder);
    if (pos > 0 && pos < this.folders.length) {
      this.folders.set_name(pos, name);
      this.mark_dirty();
      this.update_folders();
    }
  }

  move_folder_up(folder) {
    if (this.readonly) return;
    let pos = this.folderno(folder);
    if (pos > 0 && pos < this.folders.length) {
      // Swap with previous folder.
      let tmp_name = this.folders.name(pos);
      let tmp_value = this.folders.value(pos);
      this.folders.set_name(pos, this.folders.name(pos - 1));
      this.folders.set_value(pos, this.folders.value(pos - 1));
      this.folders.set_name(pos - 1, tmp_name);
      this.folders.set_value(pos - 1, tmp_value);
      this.mark_dirty();
      this.update_folders();
    }
  }

  move_folder_down(folder) {
    if (this.readonly) return;
    let pos = this.folderno(folder);
    if (pos >= 0 && pos < this.folders.length - 1) {
      // Swap with next folder.
      let tmp_name = this.folders.name(pos);
      let tmp_value = this.folders.value(pos);
      this.folders.set_name(pos, this.folders.name(pos + 1));
      this.folders.set_value(pos, this.folders.value(pos + 1));
      this.folders.set_name(pos + 1, tmp_name);
      this.folders.set_value(pos + 1, tmp_value);
      this.mark_dirty();
      this.update_folders();
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
        this.mark_dirty();
        this.update_folders();
        this.update_topics();
      }
    }
  }

  async update_folders() {
    await this.find("folder-list").update({
      folders: this.folders,
      current: this.folder,
      readonly: this.readonly
    });
  }

  async add_topic(itemid, name) {
    if (this.readonly) return;

    // Create new topic.
    let topicid = this.next_topic();
    let topic = store.frame(`t/${this.caseid()}/${topicid}`);
    if (itemid) topic.add(n_is, store.lookup(itemid));
    if (name) topic.add(n_name, name);
    this.mark_dirty();

    // Add topic to current folder.
    this.topics.push(topic);
    this.folder.push(topic);

    // Update topic list.
    await this.update_topics();
    this.navigate_to(topic);
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
    this.update_topics();
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
    this.update_topics();
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
    this.update_topics();
  }

  async update_topics() {
    await this.find("topic-list").update(this.folder);
  }

  async navigate_to(topic) {
    if (!this.folder.includes(topic)) {
      // Switch to folder with topic.
      for (let [name, folder] of this.folders) {
        if (folder.includes(topic)) {
          await this.show_folder(folder);
          break;
        }
      }
    }

    // Scroll to topic in folder.
    this.find("topic-list").navigate_to(topic);
  }

  prerender() {
    return `
      <md-column-layout>
        <md-toolbar>
          <md-icon-button id="menu" icon="menu"></md-icon-button>
          <md-toolbar-logo></md-toolbar-logo>
          <div id="title">Case #<md-text id="caseid"></md-text></div>
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
  }

  async onquery(e) {
    let detail = e.detail
    let target = e.target;
    let query = detail.trim();

    // Do full match if query ends with period.
    let full = false;
    if (query.endsWith(".")) {
      full = true;
      query = query.slice(0, -1);
    }

    // Get local results.
    let items = [];
    for (let result of this.match("#editor").search(query, full)) {
      items.push(new material.MdSearchResult({
        ref: result.id,
        name: result.get(n_name),
        description: result.get(n_description),
        topic: result,
      }));
    }

    // Get global results.
    try {
      let params = "fmt=cjson";
      if (full) params += "&fullmatch=1";
      params += `&q=${encodeURIComponent(query)}`;
      let response = await fetch(`${settings.kbservice}/kb/query?${params}`);
      let data = await response.json();
      for (let item of data.matches) {
        items.push(new material.MdSearchResult({
          ref: item.ref,
          name: item.text,
          title: item.text + " 🌍",
          description: item.description,
        }));
      }
    } catch (error) {
      console.log("Query error", query, error.message, error.stack);
      material.StdDialog.error(error.message);
      target.populate(detail, null);
      return;
    }

    target.populate(detail, items);
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
        <md-search placeholder="Search for topic..." min-length=2>
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
    if (!this.state) return;
    let folders = this.state.folders;
    let current = this.state.current;
    let readonly = this.state.readonly
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
    if (this.state.publish) {
      this.find("#publish").update(true);
    } else if (this.state.share) {
      this.find("#share").update(true);
    } else {
      this.find("#private").update(true);
    }
  }

  submit() {
    let publish = this.find("#publish").checked;
    let share = publish || this.find("#share").checked;
    this.close({share, publish});
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
          id="publish"
          name="sharing"
          value="2"
          label="Publish (case topics in public knowledge base)">
        </md-radio-button>
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

  onconnected() {
    this.bind(null, "keydown", e => this.onkeydown(e));
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
    this.scrollTop = 0;
  }

  onkeydown(e) {
    //console.log("keydown", e.key);
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

  navigate_to(topic) {
    let card = this.card(topic);
    if (card) {
      card.scrollIntoView();
      this.select(topic)
    }
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

    let existing = new Map();
    for (let e = this.firstChild; e; e = e.nextSibling) {
      existing[e.state] = e;
    }

    let h = [];
    for (let topic of topics) {
      let e = existing[topic];
      if (!e) e = new TopicCard(topic);
      h.push(e);
    }
    return h;
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
    this.update_name();
  }

  onupdated() {
    this.update_name();
  }

  update_mode(editing) {
    this.editing = editing;

    let content = this.match("md-content");
    let scrollpos = content ? content.scrollTop : undefined;
    if (editing) {
      this.find("item-editor").update(this.state);
      this.find("item-panel").update();
    } else {
      this.find("item-panel").update(this.state);
      this.find("item-editor").update();
    }
    if (scrollpos) content.scrollTop = scrollpos;

    this.find("#topic-actions").update(!editing && !this.readonly);
    this.find("#edit-actions").update(editing && !this.readonly);
  }

  update_name() {
    let name = this.state.get(n_name);
    if (!name) name = "(no name)";
    this.find("#name").update(name.toString());
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
  }

  async onsave(e) {
    e.stopPropagation();
    let edit = this.find("item-editor");
    let content = edit.value();
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

  async ondiscard(e) {
    let topic = this.state;
    let result = await material.StdDialog.confirm(
      "Discard changes",
      `Discard changes to topic '${topic.get(n_name)}'?`,
      "Discard");
    if (result) {
      this.update(this.state);
      this.update_mode(false);
    } else {
      this.find("item-editor").focus();
    }
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
      <item-panel></item-panel>
      <item-editor><item-editor>
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
      $ #edit-actions {
        display: flex;
      }
    `;
  }
}

Component.register(TopicCard);

class ItemEditor extends Component {
  visible() {
    return this.state;
  }

  onupdated() {
    if (!this.state) return;
    this.bind("md-search", "enter", e => this.onenter(e));

    this.bind("#prop-search md-search", "item", e => this.onitem(e, true));
    this.bind("#topic-search md-search", "item", e => this.onitem(e, false));
    this.bind("#prop-search md-search", "enter", e => this.onenter(e, true));
    this.bind("#topic-search md-search", "enter", e => this.onenter(e, false));
    this.bind("textarea", "input", e => this.adjust());
    this.adjust();
    this.find("textarea").focus();
  }

  value() {
    return this.find("textarea").value;
  }

  adjust() {
    let textarea = this.find("textarea");
    textarea.style.height = textarea.scrollHeight + "px";
  }

  onitem(e, isprop) {
    e.target.clear();
    let item = e.detail;
    let text = item.ref;
    if (isprop) text += ": "
    this.insert(text);

    if (isprop) {
      this.find("#topic-search input").focus();
    } else {
      this.find("textarea").focus();
    }
  }

  onenter(e, isprop) {
    e.target.clear();
    let name = e.detail;
    let text = JSON.stringify(name);
    if (isprop) text += ": "
    this.insert(text);

    if (isprop) {
      this.find("#topic-search input").focus();
    } else {
      this.find("textarea").focus();
    }
  }

  insert(text) {
    let textarea = this.find("textarea");
    let start = textarea.selectionStart;
    let end = textarea.selectionEnd;
    textarea.setRangeText(text, start, end, "end");
  }

  render() {
    let topic = this.state;
    let content = topic ? topic.text(true) : "";
    return `
      <div id="search-box">
        <property-search-box id="prop-search"></property-search-box>
        <topic-search-box id="topic-search"></topic-search-box>
      </div>
      <textarea>${Component.escape(content)}</textarea>
    `
  }

  static stylesheet() {
    return `
      $ #search-box {
        display: flex;
      }
      $ property-search-box {
        border: 1px solid #d0d0d0;
        margin: 10px 5px 5px 0px;
      }
      $ topic-search-box {
        border: 1px solid #d0d0d0;
        margin: 10px 0px 5px 5px;
      }
      $ textarea {
        font-size: 12px;
        box-sizing: border-box;
        resize: none;
        width: 100%;
        height: auto;
        overflow-y: hidden;
        border: 1px solid #d0d0d0;
      }
    `;
  }
}

Component.register(ItemEditor);

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


// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {MdDialog, StdDialog, MdSearchResult} from "/common/lib/material.js";
import {Store, Frame, Encoder, Printer} from "/common/lib/frame.js";

import {store, settings} from "./global.js";
import * as plugins from "./plugins.js";
import {NewFolderDialog} from "./folder.js";
import {paste_image} from "./drive.js";
import "./topic.js";

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
const n_media = store.lookup("media");
const n_case_file = store.lookup("Q108673968");
const n_instance_of = store.lookup("P31");

const binary_clipboard = false;

function write_to_clipboard(topics) {
  if (binary_clipboard) {
    // Encode selected topics.
    let encoder = new Encoder(store);
    for (let topic of topics) {
      encoder.encode(topic);
    }
    let clip = store.frame();
    clip.add(n_topics, topics);
    encoder.encode(clip);

    // Add selected topics to clipboard.
    var blob = new Blob([encoder.output()], {type: "text/plain"});
    let item = new ClipboardItem({"text/plain": blob});
    return navigator.clipboard.write([item]);
  } else {
    // Convert selected topics to text.
    let printer = new Printer(store);
    for (let topic of topics) {
      printer.print(topic);
    }
    let clip = store.frame();
    clip.add(n_topics, topics);
    printer.print(clip);

    // Add selected topics to clipboard.
    var blob = new Blob([printer.output], {type: "text/plain"});
    let item = new ClipboardItem({"text/plain": blob});
    return navigator.clipboard.write([item]);
  }
}

async function read_from_clipboard() {
  let clipboard = await navigator.clipboard.read();
  for (let i = 0; i < clipboard.length; i++) {
    if (clipboard[i].types.includes("text/plain")) {
      // Get data from clipboard.
      let blob = await clipboard[i].getType("text/plain");
      let data = await blob.arrayBuffer();

      // Try to parse data SLING frames if it starts with \0 or '{'.
      let bytes = new Uint8Array(data);
      if (bytes[0] == 0 || bytes[0] == 123) {
        try {
          let store = new Store();
          let obj = await store.parse(data);
          return obj;
        } catch (error) {
          console.log("ignore sling parse error", error);
        }
      }
    }
  }
  return null;
}

class CaseEditor extends Component {
  oninit() {
    this.bind("md-search", "item", e => this.onitem(e));
    this.bind("md-search", "enter", e => this.onenter(e));

    this.bind("#menu", "click", e => this.onmenu(e));
    this.bind("#home", "click", e => this.close());
    this.bind("#merge", "click", e => this.onmerge(e));
    this.bind("#save", "click", e => this.onsave(e));
    this.bind("#share", "click", e => this.onshare(e));
    this.bind("#newfolder", "click", e => this.onnewfolder(e));

    this.bind(null, "cut", e => this.oncut(e));
    this.bind(null, "copy", e => this.oncopy(e));
    this.bind(null, "paste", e => this.onpaste(e));

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
    e.preventDefault();
    e.stopPropagation();
    let ref = e.detail;
    let item = store.find(ref);
    if (item && (this.topics.includes(item) || this.scraps.includes(item))) {
      this.navigate_to(item);
    } else {
      window.open(`${settings.kbservice}/kb/${ref}`, "_blank");
    }
  }

  onkeydown(e) {
    if (e.ctrlKey && e.key === 's') {
      e.preventDefault();
      this.onsave(e);
    } else if (e.ctrlKey && e.key === 'm') {
      this.onmerge(e);
    } else if (e.key === "Escape") {
      this.find("#search").clear();
    }
  }

  onenter(e) {
    let name = e.detail;
    this.add_new_topic(null, name.trim());
  }

  async onitem(e) {
    let item = e.detail;
    if (item.onitem) {
      await item.onitem(item);
      if (item.context) await item.context.refresh();
    } else if (item.topic) {
      if (item.casefile) {
        this.add_topic_link(item.topic);
      } else {
        this.navigate_to(item.topic);
      }
    } else if (item.caserec) {
      this.add_case_link(item.caserec);
    } else {
      this.add_new_topic(item.ref, item.name);
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

      // Delete scraps.
      this.purge_scraps();

      // Save case to local database.
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
      this.purge_scraps();
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
        StdDialog.error(`Error ${r.status} sharing case: ${r.statusText}`);
      }
    }
  }

  async onupdate() {
    if (!this.state) return;
    this.mark_clean();

    // Initialize case editor for new case.
    this.casefile = this.state;
    this.main = this.casefile.get(n_main);
    this.topics = this.casefile.get(n_topics);
    this.folders = this.casefile.get(n_folders);
    this.folder = this.casefile.get(n_folders).value(0);
    this.scraps = [];
    this.readonly = this.casefile.get(n_link);

    // Read linked cases.
    this.links = [];
    for (let topic of this.topics) {
      if (topic != this.main && topic.get(n_instance_of) == n_case_file) {
        let linkid = topic.get(n_is).id;
        let caseid = parseInt(linkid.substring(2));
        let casefile = await this.match("#app").read_case(caseid);
        if (casefile) {
          this.links.push(casefile);
        } else {
          console.log("Unable to retrieve linked case", linkid);
        }
      }
    }

    // Enable/disable action buttons.
    for (let e of ["#save", "#share", "#merge", "#newfolder"]) {
      this.find(e).update(!this.readonly);
    }
  }

  async onupdated() {
    if (this.state) {
      this.find("#caseid").update(this.caseid().toString());
      this.find("md-drawer").update(true);
      await this.update_folders();
      await this.update_topics();
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

  search(query, full, topics) {
    query = query.toLowerCase();
    let results = [];
    let partial = [];
    for (let topic of (topics || this.topics)) {
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

  refcount(topic) {
    let refs = 0;
    let f = this.folders;
    for (let i = 0; i < f.length; ++i) {
      if (f.value(i).includes(topic)) refs += 1;
    }
    return refs;
  }

  redirect(source, target) {
    let topics = this.topics;
    if (this.scraps.length > 0) topics = topics.concat(this.scraps);
    for (let topic of topics) {
      for (let n = 0; n < topic.length; ++n) {
        let v = topic.value(n);
        if (source == v) {
          topic.set_value(n, target);
        } else if (v instanceof Frame) {
          if (v.isanonymous() && v.has(n_is)) {
            for (let m = 0; m < v.length; ++m) {
              if (source == v.value(m)) {
                v.set_value(m, target);
              }
            }
          }
        }
      }
    }
  }

  async show_folder(folder) {
    if (folder != this.folder) {
      this.folder = folder;
      await this.update_folders();
      await this.update_topics();
      if (folder.length > 0) {
        await this.navigate_to(folder[0]);
      }
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
      StdDialog.alert("Cannot delete folder",
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

  add_topic(topic) {
    // Add topic to current folder.
    if (this.readonly) return;
    if (!this.topics.includes(topic)) this.topics.push(topic);
    if (!this.folder.includes(topic)) this.folder.push(topic);
    this.mark_dirty();
  }

  new_topic() {
    // Create frame for new topic.
    if (this.readonly) return;
    let topicid = this.next_topic();
    let topic = store.frame(`t/${this.caseid()}/${topicid}`);

    // Add topic to current folder.
    this.add_topic(topic);

    return topic;
  }

  async add_new_topic(itemid, name) {
    // Create new topic.
    if (this.readonly) return;
    let topic = this.new_topic();
    if (itemid) topic.add(n_is, store.lookup(itemid));
    if (name) topic.add(n_name, name);

    // Update topic list.
    await this.update_topics();
    this.navigate_to(topic);
  }

  async add_case_link(caserec) {
    // Create new topic with reference to external case.
    let topic = this.new_topic();
    topic.add(n_is, store.lookup(`c/${caserec.id}`));
    topic.add(n_instance_of, n_case_file);
    if (caserec.name) topic.add(n_name, caserec.name);

    // Read case and add linked case.
    let casefile = await this.match("#app").read_case(caserec.id);
    if (casefile) {
      this.links.push(casefile);
    }

    // Update topic list.
    await this.update_topics();
    this.navigate_to(topic);
  }

  async add_topic_link(topic) {
    // Create new topic with reference to topic in external case.
    let link = this.new_topic();
    link.add(n_is, topic);
    let name = topic.get(n_name);
    if (name) link.add(n_name, name);

    // Update topic list.
    await this.update_topics();
    await this.navigate_to(link);
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
      let pos = this.folder.indexOf(topic);
      if (pos != -1) {
        this.folder.splice(pos, 1);
      } else {
        console.log("topic not in current folder", topic.id);
      }

      // Delete topic from case.
      if (this.refcount(topic) == 0) {
        if (this.folder == this.scraps) {
          // Delete draft topic from case and redirect all references to it.
          console.log("delete topic", topic.id);
          let target = topic.get(n_is);
          if (!target) target = topic.get(n_name);
          if (!target) target = topic.id;
          this.redirect(topic, target);
        } else {
          // Move topic to scraps.
          let pos = this.topics.indexOf(topic);
          if (pos != -1) {
            this.topics.splice(pos, 1);
          } else {
            console.log("topic not found", topic.id);
          }
          this.scraps.push(topic);
        }
      }
    }
    this.mark_dirty();

    // Update folder list if scraps was added.
    let scraps_after = this.scraps.length > 0;
    if (scraps_before != scraps_after) await this.update_folders();

    // Update topic list and navigate to next topic.
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

  async purge_scraps() {
    if (this.scraps.length > 0) {
      // Remove all references to draft topics.
      for (let topic of this.scraps) {
        let target = topic.get(n_is);
        if (!target) target = topic.get(n_name);
        if (!target) target = topic.id;
        this.redirect(topic, target);
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
    this.mark_dirty();

    // Update topic list.
    await this.update_topics();
    await this.navigate_to(topic);
  }

  oncut(e) {
    // Get selected topics.
    if (this.readonly) return;
    if (this.folder == this.scraps) return this.oncopy(e);
    let list = this.find("topic-list");
    let selected = list.selection();
    if (selected.length == 0) return;
    console.log(`cut ${selected.length} topics to clipboard`);

    // Copy selected topics to clipboard.
    write_to_clipboard(selected);

    // Delete selected topics.
    this.delete_topics(selected);
  }

  oncopy(e) {
    // Allow copying of selected text.
    let s = window.getSelection();
    let anchor_text = s.anchorNode && s.anchorNode.nodeType == Node.TEXT_NODE;
    let focus_text = s.focusNode && s.focusNode.nodeType == Node.TEXT_NODE;
    if (anchor_text && focus_text) return;

    // Get selected topics.
    let list = this.find("topic-list");
    let selected = list.selection();
    if (selected.length == 0) return;
    e.stopPropagation();
    console.log(`copy ${selected.length} topics to clipboard`);

    // Copy selected topics to clipboard.
    write_to_clipboard(selected);
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
    if (clip instanceof Frame) {
      let first = null;
      let last = null;
      let topics = clip.get("topics");
      if (topics) {
        let scraps_before = this.scraps.length > 0;
        let import_mapping = new Map();
        for (let t of topics) {

          // Determine if pasted topic is from this case.
          let topic = store.find(t.id);
          let external = true;
          if (topic) {
            let in_topics = this.topics.includes(topic);
            if (in_topics) {
              // Add link to topic in current folder.
              console.log("paste topic link", t.id);
              this.add_topic(topic);
              external = false;
            } else {
              let draft_pos = this.scraps.indexOf(topic);
              if (draft_pos != -1) {
                // Move topic from scraps to current folder.
                console.log("undelete", topic.id);
                this.add_topic(topic);
                this.scraps.splice(draft_pos, 1);
                external = false;
              }
            }
          }

          // Copy topic if it is external.
          if (external) {
            topic = this.new_topic();
            import_mapping.set(t.id, topic);
            console.log("paste external topic", t.id, topic.id);
            for (let [name, value] of t) {
              if (name != t.store.id) {
                topic.add(store.transfer(name), store.transfer(value));
              }
            }
          }

          if (!first) first = topic;
          last = topic;
        }

        // Redirect imported topic ids.
        for (let [id, topic] of import_mapping.entries()) {
          let proxy = store.find(id);
          if (proxy) this.redirect(proxy, topic);
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
      let result = await plugins.process(plugins.PASTE, clip, context);
      this.style.cursor = "";
      if (result) {
        await context.refresh();
        return;
      }
    }

    // Try to paste image.
    if (topic) {
      let imgurl = await paste_image();
      if (imgurl) {
        topic.add(n_media, imgurl);
        this.mark_dirty();
        await this.update_topic(topic);
        return;
      }
    }
  }

  async onmerge(e) {
    // Get selected topics.
    if (this.readonly) return;
    let list = this.find("topic-list");
    let selected = list.selection();
    if (selected.length < 2) return;

    // Merge the rest of the topics into the first topic.
    let target = selected[0];
    let sources = selected.slice(1);
    for (let topic of sources) {
      // Add properties from topic to target.
      for (let [name, value] of topic) {
        if (name != store.id) {
          target.put(name, value);
        }
      }

      // Redirect reference to topic to target.
      this.redirect(topic, target);
    }

    // Delete merged topics from folder.
    await this.delete_topics(sources);

    // Update target topic.
    this.mark_dirty();
    let card = list.card(target);
    if (card) {
      card.update(target);
      card.refresh(target);
      await this.update_topics();
      await this.navigate_to(target);
    }
  }

  async update_folders() {
    await this.find("folder-list").update({
      folders: this.folders,
      current: this.folder,
      scraps: this.scraps,
      readonly: this.readonly
    });
  }

  async update_topics() {
    await this.find("topic-list").update(this.folder);
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
          <md-icon-button id="merge" icon="merge"></md-icon-button>
          <md-icon-button id="save" icon="save"></md-icon-button>
          <md-icon-button id="share" icon="share"></md-icon-button>
        </md-toolbar>

        <md-row-layout>
          <md-drawer>
            <div id="home">
              <md-icon-button icon="home"></md-icon-button>
              SLING Case Home
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
    let editor = this.match("#editor");

    // Do full match if query ends with period.
    let full = false;
    if (query.endsWith(".")) {
      full = true;
      query = query.slice(0, -1);
    }

    // Search case file.
    let items = [];
    let seen = new Set();
    for (let result of editor.search(query, full)) {
      if (seen.has(result.id)) continue;
      let name = result.get(n_name);
      items.push(new MdSearchResult({
        ref: result.id,
        name: name,
        title: name + (result == editor.main ? " 🗄️" : " ⭐"),
        description: result.get(n_description),
        topic: result,
      }));
      seen.add(result.id);
      for (let ref of result.all(n_is)) seen.add(ref.id);
    }

    // Search linked case files.
    for (let link of editor.links) {
      let topics = link.get(n_topics);
      if (topics) {
        for (let result of editor.search(query, full, topics)) {
          if (seen.has(result.id)) continue;
          let name = result.get(n_name);
          items.push(new MdSearchResult({
            ref: result.id,
            name: name,
            title: name + " 🔗",
            description: result.get(n_description),
            topic: result,
            casefile: link,
          }));
          for (let ref of result.all(n_is)) seen.add(ref.id);
        }
      }
    }

    // Search local case database.
    for (let result of this.match("#app").search(query, full)) {
      let caseid = "c/" + result.id;
      if (seen.has(caseid)) continue;
      items.push(new MdSearchResult({
        ref: caseid,
        name: result.name,
        title: result.name + " 🗄️",
        description: result.description,
        caserec: result,
      }));
      seen.add(caseid);
    }

    // Search plug-ins.
    let context = new plugins.Context(null, editor.casefile, editor);
    let result = await plugins.process(plugins.SEARCH, query, context);
    if (result) {
      items.push(new MdSearchResult(result));
    }

    // Search knowledge base.
    try {
      let params = "fmt=cjson";
      if (full) params += "&fullmatch=1";
      params += `&q=${encodeURIComponent(query)}`;
      let response = await fetch(`${settings.kbservice}/kb/query?${params}`);
      let data = await response.json();
      for (let item of data.matches) {
        if (seen.has(item.ref)) continue;
        items.push(new MdSearchResult({
          ref: item.ref,
          name: item.text,
          description: item.description,
        }));
      }
    } catch (error) {
      console.log("Query error", query, error.message, error.stack);
      StdDialog.error(error.message);
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

class SharingDialog extends MdDialog {
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
    return MdDialog.stylesheet() + `
      $ #content {
        display: flex;
        flex-direction: column;
        row-gap: 16px;
      }
    `;
  }
}

Component.register(SharingDialog);


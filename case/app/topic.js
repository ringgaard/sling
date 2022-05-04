// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {StdDialog, MdIcon, MdDialog, inform} from "/common/lib/material.js";
import {Frame, QString, Printer} from "/common/lib/frame.js";
import {store, settings} from "./global.js";
import {get_schema, inverse_property} from "./schema.js";
import {LabelCollector, value_parser} from "./value.js";
import "./item.js"
import "./fact.js"
import "./omnibox.js"

const n_id = store.id;
const n_is = store.is;
const n_name = store.lookup("name");
const n_case_file = store.lookup("Q108673968");
const n_instance_of = store.lookup("P31");
const n_media = store.lookup("media");
const n_has_quality = store.lookup("P1552");
const n_not_safe_for_work = store.lookup("Q2716583");

// Cross-reference configuration.
var xrefs;

// Get topic card containing element.
function topic_card(e) {
  while (e) {
    if (e instanceof TopicCard) break;
    e = e.parentNode;
  }
  return e;
}

class TopicList extends Component {
  onconnected() {
    this.bind(null, "keydown", e => this.onkeydown(e));
    this.bind(null, "focusout", e => this.onfocusout(e));
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
  }

  onkeydown(e) {
    let active = this.active();
    if (active) {
      let topics = this.state;
      let pos = topics.indexOf(active);
      if (pos != -1) {
        if (e.code === "Delete") {
          this.delete_selected();
        } else if (e.code === "ArrowDown" && pos < topics.length - 1) {
          e.preventDefault();
          let next = topics[pos + 1];
          if (e.ctrlKey) {
            this.card(active).onmovedown(e);
          } else if (e.shiftKey) {
            this.select(next, true);
          } else {
            this.select(next, false);
          }
        } else if (e.code === "ArrowUp" && pos > 0) {
          e.preventDefault();
          let prev = topics[pos - 1];
          if (e.ctrlKey) {
            this.card(active).onmoveup(e);
          } else if (e.shiftKey) {
            this.select(prev, true);
          } else {
            this.select(prev, false);
          }
        } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyA") {
          e.preventDefault();
          this.select_all();
        }
      }
    }
  }

  onfocusout(e) {
    // Clear selection if focus leaves the topic list.
    if (e.target && e.relatedTarget ) {
      if (this.contains(e.relatedTarget)) return;
      if (!this.contains(e.target)) return;
    }

    window.getSelection().collapse(null, 0);
    let c = this.firstChild;
    while (c) {
      c.classList.remove("selected");
      c = c.nextSibling;
    }
  }

  select(topic, extend) {
    let card = this.card(topic);
    if (card) {
      // Get current anchor and focus topics.
      let s = window.getSelection();
      let a = s.anchorNode;
      let f = s.focusNode;
      if (a == f) a = f = document.activeElement;
      a = topic_card(a);
      f = topic_card(f);

      // Focus new card.
      card.focus();

      // Update selection.
      f = card;
      if (!extend) a = f;

      // Set new selection.
      s.setBaseAndExtent(a, 0, f, 0);
    }
  }

  select_range(anchor, focus) {
    let a = this.card(anchor);
    let f = this.card(focus);
    let selection = window.getSelection();
    if (a == f) {
      selection.collapse(a, 0);
    } else {
      selection.setBaseAndExtent(a, 0, f, 0);
    }
  }

  select_all() {
    this.select_range(this.firstChild.state, this.lastChild.state);
  }

  async delete_selected() {
    this.match("#editor").delete_topics(this.selection());
  }

  async navigate_to(topic) {
    let card = this.card(topic);
    if (card) {
      return new Promise((resolve) => {
        setTimeout((card, resolve) => {
          card.scrollIntoView({block: "center", behavior: "smooth"});
          card.focus();
          window.getSelection().collapse(card, 0);
          resolve();
        }, 100, card, resolve);
      });
    }
  }

  card(topic) {
    for (let i = 0; i < this.children.length; i++) {
      let card = this.children[i];
      if (card.state == topic) return card;
    }
    return null;
  }

  active() {
    let e = document.activeElement;
    if (e instanceof TopicCard) return e.state;
    return null;
  }

  selection() {
    let selected = new Array();
    for (let node of  this.querySelectorAll("topic-card.selected")) {
      selected.push(node.state);
    }
    if (selected.length == 0) {
      let active = this.active();
      if (active) selected.push(active);
    }
    return selected;
  }

  refresh(state) {
    this.innerHTML = "";
    return this.update(state);
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

  static stylesheet() {
    return `
      $ {
        display: block;
        padding-bottom: 500px;
      }

      $::selection {
        background-color: transparent;
        user-select: none;
      }
    `;
  }
}

Component.register(TopicList);

// Mark selected topics when selection changes.
document.onselectionchange = () => {
  // Check if selection is a range of topics.
  let selection = document.getSelection();
  let anchor = selection.anchorNode;
  let focus = selection.focusNode;
  if (!focus || !anchor) return;

  if (window.safari) {
    // Safari does not allow topic selection.
    anchor = topic_card(anchor);
    focus = topic_card(focus);
  }

  if (!(anchor instanceof TopicCard)) return;
  if (!(focus instanceof TopicCard)) return;
  if (focus.parentNode != anchor.parentNode) return;

  // Mark topics in selection.
  let list = anchor.parentNode;
  let c = list.firstChild;
  let marking = false;
  while (c) {
    if (c == anchor || c == focus) {
      if (anchor == focus) {
        c.classList.remove("selected");
      } else {
        c.classList.add("selected");
        marking = !marking && anchor != focus;
      }
    } else if (marking) {
      c.classList.add("selected");
    } else {
      c.classList.remove("selected");
    }
    c = c.nextSibling;
  }
}

class TopicCard extends Component {
  constructor(state) {
    super(state);
    this.tabIndex = -1;
  }

  oninit() {
    let editor = this.match("#editor");
    this.readonly = editor && editor.readonly;
    if (!this.readonly) {
      this.bind("#delete", "click", e => this.ondelete(e));
      this.bind("#import", "click", e => this.onimport(e));
      this.bind("#moveup", "click", e => this.onmoveup(e));
      this.bind("#movedown", "click", e => this.onmovedown(e));
      this.bind("#edit", "click", e => this.onedit(e));
      this.bind("#save", "click", e => this.onsave(e));
      this.bind("#discard", "click", e => this.ondiscard(e));
    }

    this.bind("#websearch", "click", e => this.onwebsearch(e));
    if (settings.imagesearch) {
      this.bind("#imgsearch", "click", e => this.onimgsearch(e));
    } else {
      this.find("#imgsearch").update(false);
    }
    this.bind("#copyid", "click", e => this.oncopyid(e));

    this.bind(null, "click", e => this.onclick(e));
    this.bind(null, "mousedown", e => this.ondown(e));
    this.bind(null, "keydown", e => this.onkeydown(e));
    this.bind(null, "focus", e => this.onfocus(e));

    this.bind(null, "nsfw", e => this.onnsfw(e, true));
    this.bind(null, "sfw", e => this.onnsfw(e, false));
    this.bind(null, "delimage", e => this.ondelimage(e));
    this.bind(null, "picedit", e => this.refresh());

    this.update_mode(false);
    this.update_title();
  }

  onupdated() {
    this.update_title();
  }

  update_mode(editing) {
    this.editing = editing;

    this.find("item-panel").update(this.editing ? null : this.state);
    this.find("fact-panel").update(this.editing ? this.state : null);

    this.find("#topic-actions").update(!editing && !this.readonly);
    this.find("#edit-actions").update(editing && !this.readonly);
  }

  update_title() {
    // Update topic type.
    let topic = this.state;
    let icon = "subject";
    if (topic.get(n_instance_of) == n_case_file) {
      if (topic.has(n_is)) {
        icon = "link";
      } else {
        icon = "folder_special";
        this.match("#editor").update_title();
      }
    }
    this.find("#icon").update(icon);

    // Update topic name.
    let name = topic.get(n_name);
    if (!name) name = topic.id;
    this.find("#name").update(name.toString());
  }

  mark_dirty() {
    let editor = this.match("#editor");
    let topic = this.state;
    editor.mark_dirty();
    editor.topic_updated(topic);
  }

  async refresh() {
    let topic = this.state;
    let labels = new LabelCollector(store);
    labels.add(topic);
    await labels.retrieve();

    if (this.editing) {
      this.conflict = true;
    } else {
      this.update_title();
      this.find("item-panel").update(topic);
    }
  }

  async onedit(e) {
    if (this.readonly) return;
    e.stopPropagation();
    if (e.ctrlKey) {
      let topic = this.state;
      let dialog = new RawEditDialog(topic)
      let result = await dialog.show();
      if (result) {
        this.refresh();
        this.mark_dirty();
      }
    } else {
      this.update_mode(true);
    }
  }

  onnsfw(e, nsfw) {
    e.stopPropagation();
    if (this.editing) return;

    let url = e.detail;
    let topic = this.state;
    for (let n = 0; n < topic.length; ++n) {
      if (topic.name(n) != n_media) continue;
      let v = topic.value(n);
      if (v instanceof Frame) {
        if (v.get(n_is) == url) {
          if (nsfw) {
            v.set(n_has_quality, n_not_safe_for_work);
          } else {
            v.remove(n_has_quality);
          }
          if (v.length == 1) {
            topic.set_value(n, v.get(n_is));
          }
          this.mark_dirty();
        }
      } else if (v == url) {
        if (nsfw) {
          let qualified = store.frame();
          qualified.add(n_is, v);
          qualified.add(n_has_quality, n_not_safe_for_work);
          topic.set_value(n, qualified);
          this.mark_dirty();
        }
      }
    }
  }

  ondelimage(e) {
    if (this.editing) return;
    let url = e.detail;
    let topic = this.state;
    for (let n = 0; n < topic.length; ++n) {
      if (topic.name(n) != n_media) continue;
      let v = store.resolve(topic.value(n));
      if (v == url) {
        topic.remove(n);
        this.mark_dirty();
        break;
      }
    }
  }

  async onsave(e) {
    e.stopPropagation();

    // Check for update conflict.
    if (this.conflict) {
      inform("Update conflict. Topic was updated by another participant.");
      return;
    }

    // Save changes to topic.
    let topic = this.state;
    let edit = this.find("fact-panel");
    let slots = edit.slots();
    if (!slots) {
      StdDialog.error("Illformed topic");
      return;
    }
    topic.slots = slots;

    // Add inverse properties.
    let folder_update = false;
    let editor = this.match("#editor");
    for (let [prop, value] of topic) {
      // Get inverse property.
      if (!(prop instanceof Frame)) continue
      let inverse = inverse_property(prop, topic);
      if (!inverse) continue;

      // Only add inverse properties to local topics.
      let target = store.resolve(value);
      if (!(target instanceof Frame)) continue;
      if (!editor.topics.includes(target)) continue;

      // Skip if target already has the inverse relation.
      if (target.has(inverse, topic)) continue;

      // Add inverse property, including qualifiers.
      if (target != value) {
        let q = new Frame();
        q.add(n_is, topic);
        for (let [n, v] of value) {
          if (n != n_is) q.add(n, v);
        }
        target.add(inverse, q);
      } else {
        target.add(inverse, topic);
      }

      // Update target topic if it is in the same folder.
      if (editor.folder.includes(target)) {
        await editor.update_topic(target);
      }
    }

    // Collect labels.
    let labels = new LabelCollector(store);
    labels.add(topic);
    await labels.retrieve();

    // Update topic and folder.
    this.update(topic);
    this.update_mode(false);
    this.mark_dirty();
    window.getSelection().collapse(this, 0);
    this.focus();
  }

  async ondiscard(e) {
    let discard = true;
    let editor = this.find("fact-editor");
    if (editor.dirty) {
      let topic = this.state;
      discard = await StdDialog.confirm(
        "Discard changes",
        `Discard changes to topic '${topic.get(n_name)}'?`,
        "Discard");
    }
    if (discard) {
      this.conflict = false;
      this.update_mode(false);
      window.getSelection().collapse(this, 0);
      this.focus();
    } else {
      editor.focus();
    }
  }

  async ondelete(e) {
    e.stopPropagation();
    let topic = this.state;
    this.match("#editor").delete_topic(topic);
  }

  onimport(e) {
    let topic = this.state;
    let changed = false;
    for (let ref of topic.links()) {
      for (let [name, value] of ref) {
        if (name == n_id || name == n_media) continue;
        if (topic.put(name, value)) {
          changed = true;
        }
      }
    }
    if (changed) {
      this.mark_dirty();
      this.refresh();
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
    if (e.code === "Enter" && !this.editing) {
      e.preventDefault();
      this.onedit(e);
    } else if (e.code === "KeyS" && (e.ctrlKey || e.metaKey) && this.editing) {
      e.stopPropagation();
      e.preventDefault();
      this.onsave(e);
    } else if (e.code === "Escape" && this.editing) {
      e.stopPropagation();
      e.preventDefault();
      this.ondiscard(e);
    } else if (e.ctrlKey && e.shiftKey && e.code === "KeyC") {
      e.preventDefault();
      this.oncopyid(e);
    }
  }

  onimgsearch(e) {
    let topic = this.state;
    let name = topic.get(n_name);
    if (name) {
      let query = encodeURIComponent(name);
      let url = `${settings.kbservice}/photosearch?q="${query}"`;
      if (settings.nsfw) url += "&nsfw=1";
      window.open(url, "_blank");
    }
  }

  onwebsearch(e) {
    let topic = this.state;
    let name = topic.get(n_name);
    if (name) {
      let url = "https://www.google.com/search?q=" + encodeURIComponent(name);
      window.open(url, "_blank");
    }
  }

  async oncopyid(e) {
    // Mark active.
    e.target.style.color = "black";

    // Fetch xrefs from server.
    if (!xrefs) {
      let r = await fetch("/case/xrefs");
      xrefs = await store.parse(r);
    }

    // Try to find xref property.
    let topic = this.state;
    var id;
    if (!id && topic.has(n_is)) {
      let link = topic.link();
      if (link) id = link.id;
    }
    if (!id) {
      for (let prop of xrefs.get("properties")) {
        let val = topic.get(prop);
        if (val) {
          id = prop.id + "/" + store.resolve(val);
          break;
        }
      }
    }
    if (!id && topic.id) id = topic.id;
    if (id) {
      navigator.clipboard.writeText(store.resolve(id));
    }

    // Mark done.
    setTimeout(() => {
      e.target.style.color = "#808080";
    }, 150);
  }

  ondown(e) {
    this.ofsx = e.offsetX;
    this.ofsy = e.offsetY;
  }

  onclick(e) {
    // Ignore if selecting text.
    let dx = this.ofsx - e.offsetX;
    let dy = this.ofsy - e.offsetY;
    if (Math.abs(dx) + Math.abs(dy) > 10) return;

    // Select single topic on non-shift click.
    if (!e.shiftKey) {
      window.getSelection().collapse(this, 0);
    }

    // Align selection to topics.
    TopicCard.align_selection();
  }

  onfocus(e) {
    let selection = window.getSelection();
    if (!selection.achorNode && !selection.focusNode) {
      selection.collapse(this, 0);
    }
  }

  static selection() {
    let selection = window.getSelection();
    let anchor = selection.anchorNode;
    let focus = selection.focusNode;
    let single = (anchor == focus);
    anchor = topic_card(anchor);
    focus = topic_card(focus);
    if (!single && anchor == focus) anchor = null;
    if (focus && focus.editing) focus = null;
    return {anchor, focus};
  }

  static align_selection() {
    let {anchor, focus} = TopicCard.selection();
    if (anchor && focus) {
      let selection = window.getSelection();
      if (anchor == focus) {
        selection.collapse(anchor, 0);
      } else {
        selection.setBaseAndExtent(anchor, 0, focus, 0);
      }
    }
  }

  prerender() {
    let topic = this.state;
    if (!topic) return;

    return `
      <md-card-toolbar>
        <md-icon id="icon"></md-icon>
        <md-text id="name"></md-text>
        <md-spacer></md-spacer>
        <md-toolbox id="edit-actions" sticky="1">
          <md-icon-button
            id="save"
            icon="save_alt"
            tooltip="Save topic\n(Ctrl+S)">
          </md-icon-button>
          <md-icon-button
            id="discard"
            icon="cancel"
            tooltip="Discard topic edits\n(Esc)"
            tooltip-align="right">
          </md-icon-button>
        </md-toolbox>
        <md-toolbox id="topic-actions">
          <md-icon-button
            id="edit"
            icon="edit"
            tooltip="Edit topic\n(Enter)">
          </md-icon-button>
          <md-icon-button
            id="websearch"
            icon="search"
            tooltip="Web search">
          </md-icon-button>
          <md-icon-button
            id="imgsearch"
            icon="image_search"
            tooltip="Image search">
          </md-icon-button>
          <md-icon-button
            id="copyid"
            icon="numbers"
            class="ripple"
            tooltip="Copy topic id to clipboard">
          </md-icon-button>
          <md-icon-button
            id="import"
            icon="publish"
            tooltip="Import existing topic">
          </md-icon-button>
          <md-icon-button
            id="moveup"
            icon="move-up"
            tooltip="Move topic up\n(Ctrl+Up)">
          </md-icon-button>
          <md-icon-button
            id="movedown"
            icon="move-down"
            tooltip="Move topic down\n(Ctrl+Down)">
          </md-icon-button>
          <md-icon-button
            id="delete"
            icon="delete"
            tooltip="Delete topic\n(Del)"
            tooltip-align="right">
          </md-icon-button>
        </md-toolbox>
      </md-card-toolbar>
      <item-panel></item-panel>
      <fact-panel></fact-panel>
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: block;
        background-color: rgb(255, 255, 255);
        box-shadow: rgb(0 0 0 / 15%) 0px 2px 4px 0px,
                    rgb(0 0 0 / 25%) 0px 2px 4px 0px;
        border: 1px solid white;
        padding: 10px;
        margin: 5px 5px 15px 5px;
        overflow-x: clip;
        outline: none;
      }
      $ md-card-toolbar {
        align-items: center;
      }
      $ #icon {
        color: #000000;
      }
      $ #name {
        padding-left: 5px;
        padding-right: 5px;
      }
      $:focus {
        border: 1px solid #1a73e8;
      }
      $.selected {
        background-color: #e7f0fd;
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
      $ md-icon-button {
        margin-left: -8px;
      }

      $.selected::selection {
        background-color: transparent;
        user-select: none;
      }
      $.selected ::selection {
        background-color: transparent;
        user-select: none;
      }
    `;
  }
}

Component.register(TopicCard);

class RawEditDialog extends MdDialog {
  onconnected() {
    this.bind("textarea", "keydown", e => this.onkeydown(e));
  }

  onkeydown(e) {
    if ((e.ctrlKey || e.metaKey) && e.code === "KeyS") {
      this.submit();
      e.preventDefault()
    }
    e.stopPropagation()
  }

  submit() {
    let content = this.find("textarea").value;
    try {
      let topic = store.parse(content);
      this.close(topic);
    } catch (error) {
      console.log("error", error);
      this.find("#msg").innerText = `Error: ${error.message}`;
    }
  }

  render() {
    let topic = this.state;
    let content = topic.text(true, true)
    return `
      <md-dialog-top>Edit topic code</md-dialog-top>
        <textarea
          spellcheck="false">${Component.escape(content)}</textarea>
      <md-dialog-bottom>
        <span id="msg"></span>
        <md-spacer></md-spacer>
        <button id="cancel">Cancel</button>
        <button id="submit">Update</button>
      </md-dialog-bottom>
    `;
  }

  static stylesheet() {
    return MdDialog.stylesheet() + `
      $ textarea {
        width: calc(100vw * 0.8);
        height: calc(100vh * 0.8);
      }
      $ #msg {
        color: red;
      }
    `;
  }
}

Component.register(RawEditDialog);

MdIcon.custom("move-down", `
<svg width="24" height="24" viewBox="0 0 32 32">
  <g><polygon points="30,16 22,16 22,2 10,2 10,16 2,16 16,30"/></g>
</svg>
`);

MdIcon.custom("move-up", `
<svg width="24" height="24" viewBox="0 0 32 32">
  <g><polygon points="30,14 22,14 22,28 10,28 10,14 2,14 16,0"/></g>
</svg>
`);


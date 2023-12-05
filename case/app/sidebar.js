// Copyright 2024 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Frame} from "/common/lib/frame.js";
import {store, frame, settings} from "/common/lib/global.js";
import {Component} from "/common/lib/component.js";
import {Document} from "/common/lib/document.js";
import {inform, MdDialog, StdDialog} from "/common/lib/material.js";
import {DocumentViewer} from "/common/lib/docviewer.js";

import {search, kbsearch, SearchResultsDialog} from "./search.js";
import {langcode} from "./schema.js";

const n_is = store.is;
const n_name = frame("name");
const n_description = frame("description");
const n_lex = frame("lex");
const n_bookmarked = frame("bookmarked");
const n_language = frame("P407");
const n_copyright = frame("P6216");
const n_copyrighted = frame("Q50423863");

function same(d1, d2) {
  if (!d1 || !d2) return false;
  if (d1 == d2) return true;
  if (d1.context.topic != d2.context.topic) return false;
  if (d1.context.index != d2.context.index) return false;
  if (d1.context.match != d2.context.match) return false;
  return true;
}

const work_ownership = new Set();

class SideBar extends Component {
  visible() { return this.state; }

  onconnected() {
    this.editor = this.match("#editor");
  }

  oninit() {
    this.attach(this.onedit, "edit");
    this.attach(this.onhighlight, "highlight");
    this.attach(this.onreconcile, "reconcile");
  }

  onrendered() {
    if (!this.state) return;

    let context = this.state.context;
    let source = this.state.source;
    let tocname = context.topic.get(n_name);
    let docname;
    if (source instanceof Frame) {
      docname = source.get(n_name);
    }
    this.find("#tocname").update(tocname);
    this.find("#docname").update(docname);
    this.attach(this.onmenu, "select", "md-menu");
    this.attach(this.onnavigate, "click", "#titlebox");
  }

  mark_dirty(topic) {
    this.editor.mark_dirty();
    this.editor.topic_updated(topic);
    this.editor.update_topic(topic);
  }

  async onmenu(e) {
    let command = e.target.id;
    let document = this.state;

    if (command == "close") {
      this.update(null);
    } else if (command == "next") {
      this.onnext();
    } else if (command == "prev") {
      this.onprev();
    } else if (command == "delete") {
      this.ondelete();
    } else if (command == "bookmark") {
      this.onbookmark();
    } else if (command == "edit") {
      this.onedit();
    } else if (command == "analyze") {
      this.onanalyze();
    } else if (command == "phrasematch") {
      this.onphrasematch();
    } else if (command == "topicmatch") {
      this.ontopicmatch();
    }
  }

  async onedit(e) {
    let source = this.state.source;
    let context = this.state.context;
    let topic = context.topic;
    let index = context.index;
    let mention = e && e.detail;

    let dialog = new DocumentEditDialog(source);
    if (mention) {
      dialog.sel_begin = mention.sbegin;
      dialog.sel_end = mention.send;
    }
    let result = await dialog.show();
    if (result) {
      let n = topic.slot(n_lex, index);
      topic.set_value(n, result);

      this.mark_dirty(topic);
      let newdoc = new Document(store, result, context);
      this.refresh(newdoc);
    }
  }

  async ondelete() {
    let context = this.state.context;
    let topic = context.topic;
    let index = context.index;
    let ok = await StdDialog.ask("Delete document", "Delete document?");
    if (ok) {
      let n = topic.slot(n_lex, index);
      topic.remove(n);
      this.mark_dirty(topic);
      this.update(null);
    }
  }

  onnext() {
    let context = this.state.context;
    let topic = context.topic;
    let index = context.index;

    let current = context.topic.value(context.topic.slot(n_lex, index));
    index++;
    let next = context.topic.value(context.topic.slot(n_lex, index));

    if (next) {
      if (current.get(n_bookmarked)) {
        current.remove(n_bookmarked);
        next.set(n_bookmarked, true);
        this.mark_dirty(topic);
      }
      this.update(new Document(store, next, {topic, index}));
    } else {
      inform("already at end");
    }
  }

  onprev() {
    let context = this.state.context;
    let topic = context.topic;
    let index = context.index - 1;
    let source = context.topic.value(context.topic.slot(n_lex, index));
    if (source) {
      this.update(new Document(store, source, {topic, index}));
    } else {
      inform("already at beginning");
    }
  }

  onbookmark() {
    let context = this.state.context;
    let topic = context.topic;
    let index = context.index;
    let source = topic.value(topic.slot(n_lex, index));
    if (source instanceof Frame) {
      if (source.get(n_bookmarked)) {
        source.remove(n_bookmarked);
      } else {
        source.set(n_bookmarked, true);
      }
      this.mark_dirty(topic);
    }
  }

  async onanalyze() {
    if (!settings.analyzer) {
      inform("No document analyzer configured");
      return;
    }

    let source = this.state.source;
    let context = this.state.context;
    let topic = context.topic;
    let index = context.index;

    let lang;
    if (source instanceof Frame) lang = await langcode(source.get(n_language));
    if (!lang) lang = await langcode(topic.get(n_language));

    this.style.cursor = "wait";
    let headers = {"Content-Type": "text/lex"};
    if (lang) headers["Content-Language"] = lang;
    try {
      let r = await fetch(settings.analyzer, {
        method: "POST",
        headers: headers,
        body: store.resolve(source),
      });
      if (r.ok) {
        let result = await r.text();
        if (source instanceof Frame) {
          source.set(n_is, result);
        } else {
          let n = topic.slot(n_lex, index);
          topic.set_value(n, result);
        }
        this.update(new Document(store, source, context));
        this.mark_dirty(topic);
      } else {
        inform(`Error ${r.status} in analyzer ${settings.analyzer}`);
      }
    } catch (error) {
      inform("Error analyzing document: " + error);
    }
    this.style.cursor = "";
  }

  onphrasematch() {
    let doc = this.state;
    let source = doc.source;
    let context = doc.context;

    let phrases = new Map();
    let index = 0;
    for (let lex of context.topic.all(n_lex)) {
      if (index++ == context.index) break;
      if (lex instanceof Frame) {
        for (let [k, v] of lex) {
          if (typeof(k) === 'string' && (v instanceof Frame)) {
            phrases.set(k, v);
          }
        }
      }
    }

    let updates = 0;
    for (let m of doc.mentions) {
      if (m.annotation && m.annotation.length > 0) continue;
      let phrase = m.text();
      let match = phrases.get(phrase);
      if (match) {
        if (m.annotation) {
          m.annotation.set(n_is, match);
        } else {
          m.annotation = match;
        }
        source.set(phrase, match);
        updates++;
      }
    }

    if (updates > 0) {
      this.update(new Document(store, source, context));
      this.mark_dirty(doc);
      inform(`${updates} phrase mappings added`);
    }
  }

  ontopicmatch() {
    let doc = this.state;
    let index = editor.get_index();
    let updates = 0;
    for (let m of doc.mentions) {
      if (m.annotation && m.annotation.length > 0) continue;
      let phrase = m.text();
      let match = index.find(phrase);
      if (match) {
        if (m.annotation) {
          m.annotation.set(n_is, match);
        } else {
          m.annotation = match;
        }
        source.set(phrase, match);
        updates++;
      }
    }

    if (updates > 0) {
      this.update(new Document(store, source, context));
      this.mark_dirty(doc);
      inform(`${updates} topic mappings added`);
    }
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

    // Search for matches.
    let backends = [editor.search.bind(editor), docsearch, kbsearch];
    let options = {
      full: true,
      swap: true,
      plural: true,
      submatch: true,
      local: editor.get_index(),
    };

    this.style.cursor = "wait";
    let results = await search(query, backends, options);
    this.style.cursor = "";

    // Open reconciliation dialog.
    let dialog = new SearchResultsDialog({
      title: "Reconcile with...",
      items: results});
    let ref = await dialog.show();
    if (ref === false) return;

    // Update phrase table.
    let source = mention.document.source;
    let context = mention.document.context;
    if (source instanceof Frame) {
      let text = mention.text();
      if (ref === null) {
        source.purge((name, value) => name == text);
      } else {
        source.set(text, frame(ref));
      }
    }

    // Update document.
    let newdoc = new Document(store, source, context);
    this.refresh(newdoc);
    this.mark_dirty(context.topic);
  }

  async onnavigate(e) {
    await this.editor.navigate_to(this.state.context.topic);
  }

  async onhighlight(e) {
    let mention = e.detail.mention;
    let match = store.resolve(mention.annotation);
    let context = mention.document.context;
    if (match == context.match) {
      context.match = null;
    } else {
      context.match = match;
    }
    this.refresh(mention.document);
  }

  async onupdate() {
    if (!this.state) return;
    let ok = await this.check_rights(this.state.context.topic);
    if (!ok) this.state = null;
  }

  onupdated() {
    let viewer = this.find("document-viewer");
    if (viewer) viewer.update(this.state);
  }

  refresh(newdoc) {
    if (!same(this.state, newdoc)) return;
    let viewer = this.find("document-viewer");
    let scroll = viewer.scrollTop;
    this.state = newdoc;
    viewer.update(newdoc);
    viewer.scrollTop = scroll;
  }

  async goto(doc) {
    await this.update(doc);
    this.find("document-viewer").goto(doc.context.match);
  }

  async check_rights(topic) {
    if (!this.editor.readonly) return true;
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

  render() {
    return `
      <md-resizer class="left"></md-resizer>
      <div id="main">
        <div id="banner">
          <div id="titlebox">
            <md-text id="tocname"></md-text><br>
            <md-text id="docname"></md-text>
          </div>
          <md-menu id="menu">
            <md-menu-item id="edit">Edit</md-menu-item>
            <md-menu-item id="delete">Delete</md-menu-item>
            <md-menu-item id="next">Next</md-menu-item>
            <md-menu-item id="prev">Previous</md-menu-item>
            <md-menu-item id="bookmark">Bookmark</md-menu-item>
            <md-menu-item id="analyze">Analyze</md-menu-item>
            <md-menu-item id="phrasematch">Match phrases</md-menu-item>
            <md-menu-item id="topicmatch">Match topics</md-menu-item>
            <md-menu-item id="close">Close</md-menu-item>
          </md-menu>
        </div>
        <document-viewer></document-viewer>
      </div>
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        flex-direction: row;
        position: relative;
        width: 40vw;
        min-width: 10vw;
        max-width: 70vw;
        border-left: 1px #a0a0a0 solid;
      }
      $ #main {
        display: flex;
        flex-direction: column;
        overflow: hidden;
        width: 100%;
        padding-left: 10px;
      }
      $ #banner {
        display: flex;
        flex-direction: row;
        padding: 4px 4px 4px 0px;
      }
      $ #titlebox {
        flex: 1 1 auto;
        overflow: hidden;
        white-space: nowrap;
        color: #808080;
        cursor: pointer;
      }
      $ #tocname {
        overflow: hidden;
        font-size: 12px;
      }
      $ #docname {
        overflow: hidden;
        font-size: 16px;
        font-weight: bold;
      }
      $ #menu {
        flex: none;
      }
      $ document-viewer {
        width: 100%;
        height: 100%;
        overflow: auto;
        padding: 0;
      }
      $ span.unknown {
        text-decoration: underline wavy red;
      }
    `;
  }
}

Component.register(SideBar);

class DocumentEditDialog extends MdDialog {
  onconnected() {
    this.textarea = this.find("textarea");
    this.attach(this.onkeydown, "keydown", "textarea");
    this.attach(this.onfind, "find");

    if (this.sel_begin && this.sel_end) {
      this.select(this.sel_begin, this.sel_end);
    }
  }

  onkeydown(e) {
    e.stopPropagation()
    if ((e.ctrlKey || e.metaKey) && e.code === "KeyS") {
      e.preventDefault();
      this.submit();
    } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyF") {
      e.preventDefault();
      let findbox = this.find("md-find-box");
      let start = this.textarea.selectionStart;
      let end = this.textarea.selectionEnd;
      let selected = this.textarea.value.substring(start, end);
      if (!selected) selected = this.lastsearch || "";
      findbox.update(selected);
    } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyG") {
      e.preventDefault();
      this.search(this.lastsearch, e.shiftKey);
    } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyB") {
      this.bracket();
    }
  }

  onfind(e) {
    if (e.detail) {
      let text = e.detail.text;
      let backwards = e.detail.backwards;
      this.search(text, backwards);
      this.lastsearch = text;
    } else {
      this.textarea.focus();
    }
  }

  bracket() {
    let text = this.textarea.value;
    let start = this.textarea.selectionStart;
    let end = this.textarea.selectionEnd;
    if (start == end) return;
    text = text.slice(0, start) + "[" +
           text.slice(start, end) + "]" +
           text.slice(end);
    this.textarea.value = text;
    this.textarea.selectionStart = start + 1;
    this.textarea.selectionEnd = end + 1;
  }

  search(text, backwards) {
    let content = this.textarea.value;
    this.textarea.focus();

    let pos = -1;
    if (backwards) {
      let start = this.textarea.selectionStart;
      pos = content.substring(0, start).lastIndexOf(text);
    } else {
      let end = this.textarea.selectionEnd;
      pos = content.indexOf(text, end);
    }
    if (pos == -1) return;
    this.select(pos, pos + text.length);
  }

  select(begin, end) {
    let content = this.textarea.value;
    let height = this.textarea.clientHeight;
    this.textarea.value = content.substring(0, begin);
    let y = this.textarea.scrollHeight;

    this.textarea.value = content;
    this.textarea.scrollTop = y > height ? y - height / 2 : 0;
    this.textarea.setSelectionRange(begin, end);
  }

  submit() {
    let content = this.textarea.value;
    if (this.state instanceof Frame) {
      let title = this.find("#title").value;
      this.state.set(n_name, title);
      this.state.set(n_is, content);
      content = this.state;
    }
    this.close(content);
  }

  render() {
    let content = store.resolve(this.state);
    let title = (this.state instanceof Frame) && this.state.get(n_name);
    return `
      <md-dialog-top>Edit document</md-dialog-top>
        <md-text-field
          id="title"
          value="${Component.escape(title)}"
          label="Title"
          ${title ? '' : 'class="hidden"'}
        >
        </md-text-field>
        <div class="editbox">
          <textarea
            spellcheck="false">${Component.escape(content)}</textarea>
          <md-find-box></md-find-box>
        </div>
      <md-dialog-bottom>
        <button id="cancel">Cancel</button>
        <button id="submit">Update</button>
      </md-dialog-bottom>
    `;
  }

  static stylesheet() {
    return `
      $ .editbox {
        position: relative;
      }
      $ md-find-box {
        position: absolute;
        top: 0;
        right: 0;
        background: white;
      }
      $ textarea {
        width: calc(100vw * 0.8);
        height: calc(100vh * 0.7);
        background-color: #f5f5f5;
        border: 0;
        padding: 8px;
        outline: none;
      }
      $ md-text-field {
        padding-bottom: 8px;
      }
      $ .hidden {
        display: none;
      }
    `;
  }
}

Component.register(DocumentEditDialog);


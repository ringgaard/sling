// Copyright 2023 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Document editor web component.

import {store, frame} from "./global.js";
import {Frame, QString} from "./frame.js";
import {Document} from "./document.js";
import {Component, stylesheet} from "./component.js";
import {value_text, LabelCollector} from "./datatype.js";
import "./material.js";

const n_is = store.is;
const n_isa = store.isa;
const n_phrases = frame("phrases");
const n_description = frame("description");

stylesheet("@import url(/common/font/anubis.css)");

function html_prop(prop) {
  let [text, encoded] = value_text(prop);
  return Component.escape(text);
}

function html_value(value, prop) {
  if (prop == n_is) prop = undefined;
  value = store.resolve(value);
  let [text, encoded] = value_text(value, prop);
  if (encoded) {
    let ref = value && value.id;
    if (!ref) {
      if (value instanceof Frame) {
        ref = value.text(false, true);
      } else if (value instanceof QString) {
        ref = value.stringify(store);
      } else {
        ref = value;
      }
    }
    return `<span class="link" ref="${ref}">${Component.escape(text)}</span>`;
  } else {
    return Component.escape(text);
  }
}

function isredirect(frame) {
  return frame.length == 1 && frame.name(0) == n_is;
}

function selected_mention() {
  let s = window.getSelection();
  if (!s || s.isCollapsed) return;
  let r = s.getRangeAt(0);

  // Check if selection inside mention.
  let parent = r.commonAncestorContainer;
  if (parent.nodeType == Node.TEXT_NODE) parent = parent.parentNode;
  if (parent.tagName != "MENTION") return;

  // Check if whole mention is selected.
  let start = r.startContainer;
  let end = r.endContainer;
  if (!start || start.nodeType != Node.TEXT_NODE) return;
  if (!end || end.nodeType != Node.TEXT_NODE) return;
  if (r.startOffset != 0) return;
  if (r.endOffset != end.length) return;

  return parent;
}

class AnnotationBox extends Component {
  onconnected() {
    this.attach(this.onpointerdown, "pointerdown");
  }

  onpointerdown(e) {
    e.preventDefault();
    let target = e.target;
    let ref = target.getAttribute("ref");
    let mention = this.state;

    if (ref) {
      this.dispatch("navigate", {ref, event: e}, true);
    } else if (target.id == "annotate") {
      this.dispatch("annotate", {mention, event: e}, true);
    } else if (target.id == "reconcile") {
      this.dispatch("reconcile", {mention, event: e}, true);
    } else if (target.id == "highlight") {
      this.dispatch("highlight", {mention, event: e}, true);
    } else if (target.id == "unmention") {
      this.dispatch("unmention", mention, true);
    } else if (target.id == "copy") {
      let text = mention.text(true);
      if (e.ctrlKey && (mention.annotation instanceof Frame)) {
        let item = mention.annotation.resolve();
        if (item.id) text = item.id;
      }
      navigator.clipboard.writeText(text);
    }
    this.remove();
  }

  render() {
    let mention = this.state;
    let annotation = mention.annotation;

    let h = new Array();
    h.push("<div>");
    if (!mention.document.readonly()) {
      h.push(`
        <md-icon icon="add_circle" class="action" id="annotate"></md-icon>
        <md-icon icon="join_right" class="action" id="reconcile"></md-icon>
        <md-icon icon="delete" class="action" id="unmention"></md-icon>
      `);
    }
    h.push(`
      <md-icon icon="content_copy" class="action" id="copy"></md-icon>
      <md-icon icon="square" class="action hilite" id="highlight"></md-icon>
    `);
    h.push("</div>");

    if (typeof(annotation) === 'string') {
      let url = annotation;
      let anchor = Component.escape(url);
      h.push(`<div class="url"><a href="${url}">${anchor}</a></div>`);
    } else if ((annotation instanceof Frame) && annotation.length > 0) {
      if (annotation.isanonymous() && !isredirect(annotation)) {
        let dt = annotation.get(n_isa);
        if (dt) {
          let [text, encoded] = value_text(store.resolve(annotation), null, dt);
          let v = Component.escape(text);
          h.push(`<div class="value">${v}</div>`);
        } else {
          for (let [name, value] of annotation) {
            value = store.resolve(value);
            let p = html_prop(name);
            let v;
            if ((value instanceof Frame) && value.isanonymous()) {
              let m = mention.document.mention_of(value);
              if (m) {
                let label = Component.escape(m.text(true));
                v = `<span class="docref">${label}</span>`;
              }
            }
            if (!v) v = html_value(value, name);
            h.push(`<div class="prop">${p}: ${v}</div>`);
          }
        }
      } else {
        let item = store.resolve(annotation);
        if (item.length > 0) {
          let v = html_value(item);
          h.push(`<div class="link">${v}</div>`);
          let decription = item.get(n_description);
          if (!decription) {
            let link = item.link();
            if (link) decription = link.get(n_description);
          }
          if (decription) {
            h.push(`<div class="descr">${Component.escape(decription)}</div>`);
          }
        }
      }
    }
    return h.join("");
  }

  static stylesheet() {
    return `
      $ {
        position: absolute;
        left: 0;
        top: 1em;
        z-index: 1;
        display: flex;
        flex-direction: column;

        font-family: Roboto,Helvetica,sans-serif;
        font-size: 16px;
        font-weight: normal;
        line-height: 1;

        color: black;
        background: white;
        border: 1px solid #a0a0a0;
        box-shadow: 0 14px 28px rgba(0,0,0,0.25), 0 10px 10px rgba(0,0,0,0.22);

        padding: 8px;
        cursor: default;
      }
      $ .title {
        display: flex;
        align-items: center;
      }
      $ .url {
        padding-top: 4px;
      }
      $ .prop {
        padding-top: 4px;
      }
      $ .docref {
        color: #0000dd;
        font-style: italic;
        cursor: default;
      }
      $ .docref:hover {
        text-decoration: none;
      }
      $ .link {
        color: #0000dd;
        cursor: pointer;
      }
      $ .descr {
        font-size: 12px;
      }
      $ link:hover {
        text-decoration: underline;
      }
      $ .value {
        color: #0000dd;
      }
      $ .action {
        padding: 2px;
        font-size: 20px;
        font-weight: normal;
        color: #808080;
        cursor: pointer;
        user-select: inherit;
      }
      $ .action:hover {
        text-decoration: none;
        background-color: #eeeeee;
      }
      $ .hilite {
        color: #f5e353;
      }
    `;
  }
};

Component.register(AnnotationBox);

export class DocumentEditor extends Component {
  onconnected() {
    this.dirty = false;
    this.attach(this.onmouse, "mouseover");
    this.attach(this.clear_popup, "mouseleave");
    this.attach(this.clear_popup, "selectstart");
    this.attach(this.onclick, "click");
    this.attach(this.onkeydown, "keydown");
    this.attach(this.onfind, "find");

    // Prevent other clipboard handlers in app from handling event.
    this.bind(null, "paste", e => e.stopPropagation());
  }

  onupdated() {
    if (this.dirty) this.mark_clean();
  }

  readonly() {
    return this.state?.readonly();
  }

  content() {
    return this.find(".content");
  }

  docbox() {
    return this.find(".docbox");
  }

  onrendered() {
    // Edit bar commands.
    if (!this.readonly()) {
      this.bind("#editbar", "click", e => {
        let cmd = e.target.parentElement.parentElement.id;
        this.execute(cmd);
      });
    }

    // Monitor changes to document content DOM.
    let observer = new MutationObserver((mutations) => this.mark_dirty());
    observer.observe(this.content(), {
      subtree: true,
      characterData: true,
      childList: true,
    });
  }

  mark_dirty() {
    if (!this.dirty) {
      this.dirty = true;
      this.dispatch("dirty", true, true);
    }
  }

  mark_clean() {
    if (this.dirty) {
      this.dirty = false;
      this.dispatch("dirty", false, true);
    }
  }

  save() {
    if (this.dirty) {
      let doc = this.state;
      doc.regenerate(this.content());
      doc.save();
      this.mark_clean();
      this.dispatch("saved", this, true);
    }
  }

  async onmouse(e) {
    // Ignore if ctrl, shift, or mouse button is pressed.
    if (e.ctrlKey || e.shiftKey) return;
    if (e.buttons) {
      this.clear_popup();
      return;
    }

    // Find first enclosing span.
    let span = e.target;
    while (span != this) {
      if (span == this.popup) return;
      if (span.tagName == 'MENTION') break;
      span = span.parentNode;
    }

    // Close existing popup.
    if (this.popup && !this.popup.contains(span)) {
      this.clear_popup();
    }

    // Get mention for span.
    let mid = span.getAttribute("index");
    if (!mid) return;
    let doc = this.state;
    let mention = doc.mentions[parseInt(mid)];

    // Fetch labels for annotations.
    if (mention.annotation instanceof Frame) {
      let collector = new LabelCollector(store);
      if (mention.annotation.isanonymous()) {
        collector.add(mention.annotation);
      } else {
        collector.add_item(mention.annotation);
      }
      await collector.retrieve();
    }

    // Open new annotation box popup.
    this.clear_popup();
    this.popup = new AnnotationBox(mention);
    this.docbox().append(this.popup);

    // Get paragraph line height.
    if (!this.lineheight) {
      this.lineheight = this.find(".linemeasure").offsetHeight;
    }

    // Adjust annotation box position.
    const boxwidth = Math.max(span.offsetWidth, 180);
    let top = span.offsetTop + span.offsetHeight;
    let left = span.offsetLeft;
    if (span.offsetHeight >= 2 * this.lineheight) {
      // Mention wraps around to next line.
      left = 0;
    } else {
      let overflow = left + boxwidth - this.offsetWidth;
      if (overflow > 0) {
        left -= overflow;
        if (left < 0) left = 0;
      }
    }
    this.popup.style.top = `${top}px`;
    this.popup.style.left = `${left}px`;
  }

  onkeydown(e) {
    if (!this.readonly()) {
      if ((e.ctrlKey || e.metaKey) && e.code === "KeyM") {
        this.execute("mention");
      } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyD") {
        e.preventDefault();
        e.stopPropagation();
        this.execute("clear");
      } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyS") {
        e.preventDefault();
        e.stopPropagation();
        this.execute("save");
      } else if (e.code === "Escape") {
        e.preventDefault();
        this.execute("revert");
      } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyF") {
        e.preventDefault();
        let findbox = this.find("md-find-box");
        let selection = window.getSelection();
        if (!selection.isCollapsed) {
          this.lastsearch = selection.toString();
        } else if (!this.lastsearch) {
          this.lastsearch = "";
        }
        this.searchpos = selection.getRangeAt(0);
        findbox.update(this.lastsearch);
      } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyG") {
        e.preventDefault();
        if (this.lastsearch) {
          console.log("find again");
          this.searchpos = window.getSelection()?.getRangeAt(0);
          if (this.searchpos) this.find_forward(this.lastsearch);
        }
      }
    }
  }

  find_forward(text) {
    let node = this.searchpos.endContainer;
    let ofs = this.searchpos.endOffset;
    let root = this.content();
    while (node) {
      // Seach for text in text nodex.
      if (node.nodeType == Node.TEXT_NODE) {
        let content = node.nodeValue;
        let hit = content.indexOf(text, ofs);
        if (hit != -1) {
          // Select hit.
          let range = new Range();
          range.setStart(node, hit);
          range.setEnd(node, hit + text.length);
          this.set_focus(range);
          this.searchpos = range;
          let rect = range.getBoundingClientRect();
          let y = rect.top + rect.height / 2;

          // Scroll hit into view.
          let docbox = this.docbox();
          docbox.scrollTop += y - docbox.clientHeight / 2 - docbox.clientTop;
          return;
        }
        ofs = undefined;
      }

      // Traverse DOM.
      if (node.firstChild) {
        node = node.firstChild;
      } else {
        while (!node.nextSibling) {
          node = node.parentNode;
          if (node == root) {
            this.set_focus(this.searchpos);
            return;
          }
        }
        node = node.nextSibling;
      }
    }
  }

  find_backward(text) {
    console.log("findback", text, this.searchpos);
    let node = this.searchpos.startContainer;
    let ofs = this.searchpos.startOffset;
    let root = this.content();
    while (node) {
      // Seach for text in text nodex.
      //console.log("node", node);
      if (node.nodeType == Node.TEXT_NODE) {
        let content = node.nodeValue;
        if (ofs) content = content.substring(0, ofs);
        let hit = content.lastIndexOf(text);
        if (hit != -1) {
          // Select hit.
          let range = new Range();
          range.setStart(node, hit);
          range.setEnd(node, hit + text.length);
          this.set_focus(range);
          this.searchpos = range;
          let rect = range.getBoundingClientRect();
          let y = rect.top + rect.height / 2;

          // Scroll hit into view.
          let docbox = this.docbox();
          docbox.scrollTop += y - docbox.clientHeight / 2 - docbox.clientTop;
          return;
        }
        ofs = undefined;
      }

      // Traverse DOM.
      if (node.lastChild) {
        node = node.lastChild;
      } else {
        while (!node.previousSibling) {
          node = node.parentNode;
          if (node == root) {
            this.set_focus(this.searchpos);
            return;
          }
        }
        node = node.previousSibling;
      }
    }
  }

  onfind(e) {
    if (e.detail) {
      let text = e.detail.text;
      let backwards = e.detail.backwards;
      this.lastsearch = text;
      if (backwards) {
        this.find_backward(text);
      } else {
        this.find_forward(text);
      }
    } else {
      this.set_focus(this.searchpos);
    }
  }

  onclick(e) {
    let target = e.target;
    let mid = target.getAttribute("index");
    if (!mid) return;
    let doc = this.state;
    let mention = doc.mentions[parseInt(mid)];
    let annotation = store.resolve(mention.annotation)

    e.stopPropagation();
    if (e.ctrlKey) {
      this.dispatch("annotate", {mention, event: e}, true);
    } else if (annotation && annotation.id) {
      this.dispatch("navigate", {ref: annotation.id, event: e}, true);
    } else {
      this.dispatch("reconcile", {mention, event: e}, true);
    }
  }

  clear_popup() {
    if (this.popup) {
      this.popup.remove();
      this.popup = null;
    }
  }

  visible() { return this.state && this.state.source; }

  remove_mention(mention) {
    // Remove mention.
    let elem = this.querySelector(`mention[index="${mention.index}"]`);
    let text = document.createTextNode(elem.innerText);
    elem.replaceWith(text);
    let range = document.createRange();
    range.selectNode(text);
    let selection = window.getSelection();
    selection.removeAllRanges();
    selection.addRange(range);
  }

  execute(cmd) {
    if (this.readonly()) return;
    if (cmd == "bold") {
      // Bold text.
      document.execCommand("bold");
    } else if (cmd == "italic") {
      // Italic text.
      document.execCommand("italic");
    } else if (cmd == "list") {
      // Insert bullet list.
      document.execCommand("insertUnorderedList");
    } else if (cmd == "indent") {
      // Indent text.
      document.execCommand("indent");
    } else if (cmd == "outdent") {
      // Unindent text.
      document.execCommand("outdent");
    } else if (cmd == "clear") {
      let mention = selected_mention();
      if (mention) {
        // Remove mention.
        let text = document.createTextNode(mention.innerText);
        mention.replaceWith(text);
        let range = document.createRange();
        range.selectNode(text);
        let selection = window.getSelection();
        selection.removeAllRanges();
        selection.addRange(range);
      } else {
        // Clear formatting.
        //document.execCommand("removeFormat");
        let selection = window.getSelection();
        if (selection && !selection.isCollapsed) {
          // Replace selection with plain text.
          let text = selection.toString();
          let range = selection.getRangeAt(0);
          range.deleteContents();
          range.insertNode(document.createTextNode(text));

          // Remove empty mentions.
          let container = range.commonAncestorContainer;
          for (let elem of container.querySelectorAll("mention")) {
            if (elem.innerText == "") elem.remove();
          }
        }
      }
    } else if (cmd == "title") {
      // Headline.
      let text = window.getSelection().toString();
      document.execCommand("delete");
      document.execCommand('insertHTML', false, `<h2>${text}</h2>`);
    } else if (cmd == "mention") {
      // Add new mention.
      let selection = window.getSelection();
      if (selection && !selection.isCollapsed) {
        // Create new mention.
        let text = selection.toString();
        let doc = this.state;
        let mention = doc.add_mention();
        mention.content = text;

        // Add mention span to document.
        let range = selection.getRangeAt(0);
        range.deleteContents();
        let elem = document.createElement("mention");
        elem.setAttribute("index", mention.index);
        elem.append(document.createTextNode(text));
        range.insertNode(elem);
        range.collapse();

        // Set mention level.
        let e = elem;
        let level = 0;
        while (e != this) {
          if (e.tagName == 'MENTION') level++;
          e = e.parentNode;
        }
        elem.classList.add("l" + level);
        elem.classList.add("unknown");
      }
    } else if (cmd == "save") {
      this.save();
    } else if (cmd == "revert") {
      this.dispatch("revert", this, true);
    }
  }

  goto(topic) {
    // Find first mention.
    let doc = this.state;
    let m = doc.first_mention(topic);
    if (!m) return;

    // Scroll mention into view.
    let span = this.querySelector(`mention[index="${m.index}"]`);
    if (span) span.scrollIntoView({block: "center"});
  }

  async refocus(scope) {
    if (this.readonly()) {
      return await scope();
    } else {
      // Save and restore scroll position and selection in edit mode.
      let s = window.getSelection();
      let scroll = this.docbox().scrollTop;
      let position = s.getRangeAt(0);
      let ret = await scope();
      this.docbox().scrollTop = scroll;
      this.docbox().focus();
      s.removeAllRanges();
      s.addRange(position);
      return ret;
    }
  }

  set_focus(position) {
    if (position) {
      let s = window.getSelection();
      s.removeAllRanges();
      s.addRange(position);
    }
  }

  update_mention_status(mention, unknown) {
    let elem = this.querySelector(`mention[index="${mention.index}"]`);
    if (unknown) {
      elem?.classList.add("unknown");
    } else {
      elem?.classList.remove("unknown");
    }
  }

  render() {
    // Generate document as HTML.
    let doc = this.state;
    if (!doc) return;

    let content = doc.tohtml();
    let editable = !this.readonly();
    let h = new Array();
    h.push("<md-find-box></md-find-box>");

    h.push(`
      <div class="docbox">
        <div class="content" spellcheck="false"
             contenteditable="${editable}">${content}</div>
        <p class="footer"><span class="linemeasure">M</span></p>
      </div>
    `);

    if (editable) {
      h.push(`
        <div id="editbox">
          <div id="editbar">
            <md-icon-button id="mention" icon="data_array"></md-icon-button>
            <md-icon-button id="clear" icon="format_clear"></md-icon-button>
            <md-icon-button id="bold" icon="format_bold"></md-icon-button>
            <md-icon-button id="italic" icon="format_italic"></md-icon-button>
            <md-icon-button id="list" icon="format_list_bulleted">
            </md-icon-button>
            <md-icon-button id="indent" icon="format_indent_increase">
            </md-icon-button>
            <md-icon-button id="outdent" icon="format_indent_decrease">
            </md-icon-button>
            <md-icon-button id="title" icon="title"></md-icon-button>
            <md-icon-button id="save" icon="save_alt"></md-icon-button>
            <md-icon-button id="revert" icon="cancel"></md-icon-button>
          </div>
        </div>
      `);
    }

    return h.join("");
  }

  static stylesheet() {
    return `
      $ {
        font: 1rem anubis, serif;
        line-height: 1.5;
        padding: 4px 8px;
        position: relative;
      }
      $ .docbox {
        position: relative;
        width: 100%;
        height: 100%;
        overflow: auto;
      }
      $ .content {
        outline: none;
      }
      $ .content:empty::before {
        content: "(empty)";
        color: #aaaaaa;
      }
      $ .linemeasure {
        visibility: hidden;
      }
      $ p {
        margin-right: 8px;
        margin-top: 0.5em;
        margin-bottom: 0.5em
      }
      $ h1 {
        line-height: 1;
      }
      $ mention {
        color: #0000dd;
        cursor: pointer;
      }
      $ mention.highlight {
        background-color: #fce94f;
        padding: 4px 0 4px 0;
      }
      $ mention:hover {
        text-decoration: underline;
      }
      $ mention.l1:hover .l1 {
        color: blue;
      }
      $ mention.l1:hover .l2 {
        color: green;
      }
      $ mention.l1:hover .l3 {
        color: red;
      }
      $ mention.l1:hover .l4 {
        color: orange;
      }
      $ aside {
        background-color: #ecf0f1;
        padding: 6px;
        margin: 0px 24px;
      }
      $ .footer {
        height: 64px;
      }
      $ md-find-box {
        position: absolute;
        top: 0;
        right: 0;
        background: white;
        font-family: Roboto,Helvetica,sans-serif;
      }
      $ #editbox {
        display: flex;
        position: absolute;
        bottom: 8px;
        left: 0;
        right: 0;
        justify-content: center;
        padding-bottom: 16px;
      }
      $ #editbar {
        display: flex;
        color: white;
        background: #808080;
        padding: 4px 12px 4px 12px;
        border-radius: 12px;
      }
      $ #editbar md-icon-button button {
        height: 32px;
        width: 32px;
      }
    `;
  }
};

Component.register(DocumentEditor);


// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {MdCard} from "/common/lib/material.js";
import {store, settings} from "./global.js";

const n_id = store.id;
const n_is = store.is;
const n_name = store.lookup("name");
const n_caseno = store.lookup("caseno");
const n_topics = store.lookup("topics");
const n_folders = store.lookup("folders");
const n_next = store.lookup("next");

//-----------------------------------------------------------------------------
// Case Editor
//-----------------------------------------------------------------------------

export class CaseEditor extends Component {
  onconnected() {
    this.app = this.match("#app");
    this.bind("#back", "click", e => this.onback(e));
    this.bind("#save", "click", e => this.onsave(e));
    document.addEventListener("keydown", e => this.onkeydown(e));
  }

  onkeydown(e) {
    if (e.ctrlKey && e.key === 's') {
      e.preventDefault();
      this.onsave(e);
    }
  }

  caseno() {
    return this.casefile.get(n_caseno);
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

  onupdated() {
    if (!this.state) return;
    this.mark_clean();

    this.casefile = this.state;
    this.topics = this.casefile.get(n_topics);
    this.folder = this.casefile.get(n_folders).value(0);
    this.find("#caseno").update(this.caseno().toString());
    this.find("topic-list").update(this.folder);
  }

  onsave(e) {
    if (this.dirty) {
      this.match("#app").save_case(this.casefile);
      this.mark_clean();
    }
  }

  onback() {
    app.show_manager();
  }

  add_topic(itemid, name) {
    // Create new topic.
    let topicid = this.next_topic();
    let topic = store.frame(`t/${this.caseno()}/${topicid}`);
    if (itemid) topic.add(n_is, store.lookup(itemid));
    if (name) topic.add(n_name, name);

    // Add topic to current folder.
    this.topics.push(topic);
    this.folder.push(topic);

    // Update topic list.
    this.find("topic-list").update(this.folder);
    this.mark_dirty();
  }

  prerender() {
    return `
      <md-column-layout>
        <md-toolbar>
          <md-icon-button id="back" icon="menu"></md-icon-button>
          <md-toolbar-logo></md-toolbar-logo>
          <div id="title">Case #<md-text id="caseno"></md-text></div>
          <topic-search-box id="search"></topic-search-box>
          <md-spacer></md-spacer>
          <md-icon-button id="save" icon="save"></md-icon-button>
        </md-toolbar>

        <md-content>
          <topic-list></topic-list>
        </md-content>
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
    `;
  }
}

Component.register(CaseEditor);

class TopicSearchBox extends Component {
  onconnected() {
    this.bind("md-search", "query", e => this.onquery(e));
    this.bind("md-search", "item", e => this.onitem(e));
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

    this.itemnames = new Map();
    fetch(`${settings.kbservice}/kb/query?${params}`)
    .then(response => response.json())
    .then((data) => {
      let items = [];
      for (let item of data.matches) {
        let elem = document.createElement("md-search-item");
        elem.setAttribute("name", item.text);
        elem.setAttribute("value", item.ref);

        let title = document.createElement("span");
        title.className = "item-title";
        title.appendChild(document.createTextNode(item.text));
        elem.appendChild(title);

        if (item.description) {
          let desciption = document.createElement("span");
          desciption.className = "item-description";
          desciption.appendChild(document.createTextNode(item.description));
          elem.appendChild(desciption);
        }

        items.push(elem);
        this.itemnames[item.ref] = item.text;
      }
      target.populate(detail, items);
    })
    .catch(error => {
      console.log("Query error", query, error.message, error.stack);
      StdDialog.error(error.message);
      target.populate(detail, null);
    });
  }

  onitem(e) {
    let topic = e.detail;
    let name = this.itemnames[topic];
    this.match("#editor").add_topic(topic, name);
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

      $ .item-title {
        font-weight: bold;
        display: block;
        padding: 2px 10px 2px 10px;
      }

      $ .item-description {
        display: block;
        padding: 0px 10px 0px 10px;
      }
    `;
  }
}

Component.register(TopicSearchBox);

export class TopicList extends Component {
  onconnected() {
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

export class TopicCard extends MdCard {
  render() {
    let topic = this.state;
    if (!topic) return;

    return `
      <div id="name">${topic.get(n_name)}</div>
      <div id="id">${topic.get(n_id)}</div>
      <pre>${Component.escape(topic.text(true))}</pre>
    `;
  }

  static stylesheet() {
    return MdCard.stylesheet() + `
      $ #name {
        display: block;
        font-size: 28px;
      }
      $ #id {
        display: block;
        font-size: 13px;
        color: #808080;
        text-decoration: none;
        width: fit-content;
        outline: none;
      }
    `;
  }
}

Component.register(TopicCard);


// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {MdCard} from "/common/lib/material.js";
import {store} from "./global.js";

const n_id = store.id;
const n_name = store.lookup("name");
const n_caseno = store.lookup("caseno");
const n_topics = store.lookup("topics");
const n_folders = store.lookup("folders");

//-----------------------------------------------------------------------------
// Case Editor
//-----------------------------------------------------------------------------

export class CaseEditor extends Component {
  onconnected() {
    this.app = this.match("#app");
    this.bind("#back", "click", e => this.onback(e));
  }

  onupdated() {
    if (!this.state) return;
    let casefile = this.state;
    this.folder = casefile.get(n_folders).value(0);
    this.find("#caseno").update(casefile.get(n_caseno).toString());
    this.find("topic-list").update(this.folder);
  }

  onback() {
    app.show_manager();
  }

  prerender() {
    return `
      <md-column-layout>
        <md-toolbar>
          <md-icon-button id="back" icon="menu"></md-icon-button>
          <md-toolbar-logo></md-toolbar-logo>
          <div>Case #<md-text id="caseno"></md-text></div>
          <case-search-box id="search"></kb-search-box>
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
    `;
  }
}

Component.register(CaseEditor);

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


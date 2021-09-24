// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {store} from "./global.js";

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
    this.find("#caseno").update(casefile.get(n_caseno).toString());

    let code = [];
    code.push(casefile.text(true));
    for (let topic of casefile.get(n_topics)) {
      code.push(topic.text(true));
    }
    this.find("pre").innerHTML = code.join("\n");
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
          <md-card>
            <pre id="code"></pre>
          </md-card>
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


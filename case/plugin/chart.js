// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING topic widget for displaying charts using Google Charts.

import {Component} from "/common/lib/component.js";
import {MdDialog} from "/common/lib/material.js";
import {store, settings} from "/case/app/global.js";
import {ItemCollector} from "/case/app/value.js";

const n_depicts = store.lookup("P180");
const n_property = store.lookup("P2306");
const n_qualifier = store.lookup("P8379");

var gchart_loaded;

function gchart() {
  if (!gchart_loaded) {
    gchart_loaded = new Promise(resolve => {
      let body = document.getElementsByTagName("body")[0];
      let script = document.createElement("script");
      script.type = "text/javascript";
      script.onload = e => resolve();
      script.src = "https://www.gstatic.com/charts/loader.js";
      body.appendChild(script)
    });
  }
  return gchart_loaded;
}

class ChartDialog extends MdDialog {
  async init() {
    // Get all topics for chart.
    let widget = this.state;
    let topics = new ItemCollector(store);
    for (let d of widget.all(n_depicts)) {
      topics.add(store.resolve(d));
    }
    await topics.retrieve();
    console.log(topics.items);
  }

  render() {
    return `
      <div id="chart"></div>
    `;
  }
}

Component.register(ChartDialog);

export default class ChartWidget extends Component {
  onconnected() {
    this.attach(this.onclick, "click");
  }

  async onclick(e) {
    // Load Google Charts.
    await gchart();

    // Show chart dialog.
    let dialog = new ChartDialog(this.state);
    await dialog.init();
    let updated = await dialog.show();
    if (updated) {
      this.match("topic-card").mark_dirty();
    }
  }

  render() {
    return `<md-button label="Show chart"></md-button>`;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        justify-content: flex-end;
      }
    `;
  }
};

Component.register(ChartWidget);


// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING topic widget for displaying charts using Google Charts.

import {Component} from "/common/lib/component.js";
import {MdDialog} from "/common/lib/material.js";
import {store, settings} from "/case/app/global.js";

//import "https://www.gstatic.com/charts/loader.js"

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

export default class ChartWidget extends Component {
  onconnected() {
    this.attach(this.onclick, "click");
  }

  async onclick(e) {
    // Load Google Charts loader.
    await gchart();
    console.log(google);
  }

  render() {
    return `
      <md-icon-button
        icon="show_chart"
        tooltip="Show chart"
        tooltip-align="right">
      </md-icon-button>`;
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


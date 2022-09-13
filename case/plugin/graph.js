// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING topic widget for displaying graphs.

import {Component} from "/common/lib/component.js";
import {store, settings} from "/case/app/global.js";

export default class GraphWidget extends Component {
  render() {
    return `<p>Here is the Graph widget for ${this.state.get("name")}</p>`;
  }
};

Component.register(GraphWidget);


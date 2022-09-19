// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING topic widget for displaying network of topics.

import {Component} from "/common/lib/component.js";
import {MdDialog} from "/common/lib/material.js";
import {store, settings} from "/case/app/global.js";
import {Frame} from "/common/lib/frame.js";

const n_start_point = store.lookup("P1427");

// Node in graph.
class Node {
  constructor(topic) {
    this.topic = topic;
    this.edges = new Array();
  }

  add(relation, target) {
    this.edges.push(new Edge(this, relation, target));
  }
}

// Directed edge in graph.
class Edge {
  constructor(source, relation, target) {
    this.source = source;
    this.relation = relation;
    this.target = target;
  }
}

class NetworkDialog extends MdDialog {
  onconnected() {
    this.attach(this.submit, "click", ".close");
  }

  submit() {
    this.close();
  }

  render() {
    return `
      <svg>
      </svg>
      <md-icon-button class="close" icon="close"></md-icon-button>
    `;
  }

  static stylesheet() {
    return `
      $ {
        padding: 0px;
      }

      $ svg {
        width: 95vw;
        height: 95vh;
      }

      $ .close {
        position: absolute;
        top: 0;
        right: 0;

        padding: 8px;
        font-size: 24px;
      }
    `;
  }
}

Component.register(NetworkDialog);

export default class NetworkWidget extends Component {
  onconnected() {
    this.attach(this.onclick, "click");
  }

  async onclick(e) {
    // Build graph nodes reachable from the start nodes.
    let locals = this.match("#editor").topics;
    this.graph = new Map();
    let visit = [];
    for (let topic of this.state.all(n_start_point)) {
      topic = store.resolve(topic);
      if (this.graph.has(topic)) continue;
      if (!locals.includes(topic)) continue;
      let node = new Node(topic);
      this.graph.set(topic, node);
      visit.push(node);
    }
    while (visit.length > 0) {
      let node = visit.pop();
      for (let [name, value] of node.topic) {
        value = store.resolve(value);
        if (value instanceof Frame) {
          if (locals.includes(value)) {
            let target = this.graph.get(value);
            if (!target) {
              target = new Node(value);
              this.graph.set(value, target);
              visit.push(target);
            }
            node.add(name, target);
          }
        }
      }
    }

    for (let n of this.graph.values()) {
      console.log("Node", n.topic.id, n.topic.get("name").toString());
      for (let e of n.edges) {
        console.log("  Edge", e.relation.get("name").toString(), e.target.topic.get("name").toString());
      }
    }

    let dialog = new NetworkDialog(this.graph);
    let result = await dialog.show();
  }

  render() {
    return `
      <md-icon-button
        icon="hub"
        tooltip="Show network"
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

Component.register(NetworkWidget);


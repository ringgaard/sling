// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING topic widget for displaying network of topics.

import {Component} from "/common/lib/component.js";
import {MdDialog} from "/common/lib/material.js";
import {store, settings} from "/case/app/global.js";
import {Frame} from "/common/lib/frame.js";

const n_name = store.lookup("name");
const n_start_point = store.lookup("P1427");

const BOX_WIDTH = 180;
const BOX_HEIGHT = 40;
const GRID_SIZE = 20;
const MARGIN = 25;

var nextz = 1;

function snap(p) {
  return Math.round(p / GRID_SIZE) * GRID_SIZE;
}

class Point {
  constructor(x, y) {
    this.x = x;
    this.y = y;
  }

  toString() {
    return `(${this.x},${this.y})`;
  }
}

// Node in graph.
class Node {
  constructor(topic) {
    this.topic = topic;
    this.label = topic.get(n_name);
    if (this.label) this.label = this.label.toString();
    if (!this.label) this.label = topic.id;
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

  relname() {
    let name = this.relation.get(n_name);
    if (name) name = name.toString();
    if (!name) name = this.relation.id;
    return name;
  }
}

class NodeBox extends Component {
  onconnected() {
    this.attach(this.ondragstart, "dragstart");
    this.attach(this.ondragend, "dragend");
    this.attach(this.onmouseover, "mouseover");
    this.setAttribute("draggable", "true");
  }

  ondragstart(e) {
    this.dragx = e.x;
    this.dragy = e.y;
  }

  onmouseover(e) {
    this.style.zIndex = nextz++;
  }

  ondragend(e) {
    this.move(e.x - this.dragx, e.y - this.dragy);
    this.parentElement.draw();
  }

  render() {
    let node = this.state;
    let h = new Array();
    h.push('<div class="title">');
    h.push(Component.escape(node.label));
    h.push('</div>');
    for (let e of node.edges) {
      h.push('<div class="rel"><b>');
      h.push(Component.escape(e.relname()));
      h.push("</b>: ");
      h.push(Component.escape(e.target.label));
      h.push('</div>');
    }
    return h.join("");
  }

  move(dx, dy) {
    this.moveto(this.offsetLeft + dx, this.offsetTop + dy);
  }

  moveto(x, y) {
    this.style.left = snap(x) + "px";
    this.style.top = snap(y) + "px";
  }

  center() {
    return new Point(this.offsetLeft + BOX_WIDTH / 2,
                     this.offsetTop + BOX_HEIGHT / 2);
  }

  static stylesheet() {
    return `
      $ {
        position: absolute;
        width: ${BOX_WIDTH}px;
        min-height: ${BOX_HEIGHT}px;
        max-height: ${BOX_HEIGHT}px;
        border: 2px solid lightgrey;
        background: white;
        overflow: hidden;
      }

      $:hover {
        max-height: initial;
      }

      $ .title {
        font-weight: bold;
        text-align: center;
        padding-top: 2px;
        padding-bottom: 8px;
      }
      $ .rel {
        display: none;
      }
      $:hover .rel {
        display: block;
        font-size: 12px;
      }
    `;
  }
}

Component.register(NodeBox);

class NetworkDialog extends MdDialog {
  constructor(graph) {
    super(graph);
    this.nodes = Array.from(graph.values());
  }

  onconnected() {
    this.attach(this.submit, "click", ".close");
  }

  submit() {
    this.close();
  }

  render() {
    return `
      <canvas id="canvas"></canvas>
      <md-icon-button class="close" icon="close"></md-icon-button>
    `;
  }

  onrendered() {
    // Assign random positions to nodes.
    let w = this.scrollWidth - MARGIN * 2 - BOX_WIDTH;
    let h = this.scrollHeight - MARGIN * 2 - BOX_HEIGHT;
    for (let n of this.nodes) {
      let box = new NodeBox(n);
      let x = Math.random() * w + MARGIN;
      let y = Math.random() * h + MARGIN;
      n.box = box;
      box.moveto(x, y);
      this.appendChild(box);
    }

    // Resize canvas.
    let canvas = this.find("#canvas");
    canvas.width = this.scrollWidth;
    canvas.height = this.scrollHeight;

    // Draw edges.
    this.draw();
  }

  draw() {
    let canvas = this.find("#canvas");
    let ctx = canvas.getContext("2d");

    ctx.clearRect(0, 0, canvas.width, canvas.height);

    for (let n of this.nodes) {
      let a = n.box.center();
      for (let e of n.edges) {
        let b = e.target.box.center();
        ctx.beginPath();
        ctx.moveTo(a.x, a.y);
        ctx.lineTo(b.x, b.y);
        ctx.stroke();
      }
    }
  }

  static stylesheet() {
    return `
      $ {
        position: relative;
        padding: 0px;
        width: 95vw;
        height: 95vh;
        background-color: #eeeeee;
        overflow: hidden;
      }

      $ canvas {
        width: 100%;
        height: 100%;
      }

      $ .close {
        position: absolute;
        top: 0;
        right: 0;

        padding: 8px;
        color: black;
      }
    `;
  }
}

Component.register(NetworkDialog);

export default class NetworkWidget extends Component {
  onconnected() {
    this.attach(this.onclick, "click");
  }

  graph() {
    // Build graph nodes reachable from the start nodes.
    let locals = this.match("#editor").topics;
    let graph = new Map();
    let visit = [];
    for (let topic of this.state.all(n_start_point)) {
      topic = store.resolve(topic);
      if (graph.has(topic)) continue;
      if (!locals.includes(topic)) continue;
      let node = new Node(topic);
      graph.set(topic, node);
      visit.push(node);
    }
    while (visit.length > 0) {
      let node = visit.pop();
      for (let [name, value] of node.topic) {
        value = store.resolve(value);
        if (value instanceof Frame) {
          if (locals.includes(value)) {
            let target = graph.get(value);
            if (!target) {
              target = new Node(value);
              graph.set(value, target);
              visit.push(target);
            }
            node.add(name, target);
          }
        }
      }
    }

    return graph;
  }

  async onclick(e) {
    let graph = this.graph();

    /*
    for (let n of this.graph.values()) {
      console.log("Node", n.topic.id, n.label);
      for (let e of n.edges) {
        console.log("  Edge", e.relation.get("name").toString(), e.target.label);
      }
    }
    */

    let dialog = new NetworkDialog(graph);
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


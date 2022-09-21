// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING topic widget for displaying network of topics.

import {Component} from "/common/lib/component.js";
import {MdDialog} from "/common/lib/material.js";
import {store, settings} from "/case/app/global.js";
import {Time} from "/case/app/value.js";
import {Frame, QString} from "/common/lib/frame.js";


const n_name = store.lookup("name");
const n_description = store.lookup("description");
const n_type = store.lookup("P31");
const n_start_point = store.lookup("P1427");
const n_human = store.lookup("Q5");
const n_gender = store.lookup("P21");
const n_male = store.lookup("Q6581097");
const n_female = store.lookup("Q6581072");
const n_born = store.lookup("P569");
const n_died = store.lookup("P570");

const descriptors = [
  n_description,
  store.lookup("P19"),   // place of birth
  store.lookup("P20"),   // place of death
  store.lookup("P551"),  // residence
  store.lookup("P276"),  // location
  n_type,
];

const BOX_WIDTH = 180;
const BOX_HEIGHT = 40;
const GRID_SIZE = 20;
const MARGIN = 25;

const STRAIGHT_CONNECTOR = 0;
const ELBOW_CONNECTOR = 1;
const FORK_CONNECTOR = 2;

var nextz = 1;

function snap(p) {
  return Math.round(p / GRID_SIZE) * GRID_SIZE;
}

function date(d) {
  let t = new Time(store.resolve(d));
  if (t.precision > 4) t.precision = 4;
  return t.text();
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

class Node {
  constructor(topic) {
    this.topic = topic;
    this.edges = new Array();

    // Node label.
    this.label = topic.get(n_name);
    if (this.label) this.label = this.label.toString();
    if (!this.label) this.label = topic.id;

    // Node description.
    if (topic.get(n_type) == n_human) {
      let s = "";
      let gender = topic.get(n_gender);
      let born = topic.get(n_born);
      let died = topic.get(n_died);
      if (gender == n_male) s += "♂ ";
      if (gender == n_female) s += "♀ ";
      if (born) s += "* " + date(born) + " ";
      if (died) s += "† " + date(died) + " ";
      if (s.length > 0) this.description = s.trim();
    }
    if (!this,description) {
      for (let prop of descriptors) {
        let value = store.resolve(topic.get(prop));
        if (value instanceof Frame) value = value.get(n_name);
        if (value instanceof QString) value = value.toString();
        if (value) {
          this.description = value;
          break;
        }
      }
    }

  }
}

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

class Graph {
  constructor(seed, neighborhood) {
    this.nodes = new Map();
    this.edges = new Array();
    let visit = [];
    for (let topic of seed.all(n_start_point)) {
      topic = store.resolve(topic);
      if (this.nodes.has(topic)) continue;
      if (!neighborhood.includes(topic)) continue;
      let node = new Node(topic);
      this.nodes.set(topic, node);
      visit.push(node);
    }
    while (visit.length > 0) {
      let node = visit.pop();
      for (let [name, value] of node.topic) {
        value = store.resolve(value);
        if (value instanceof Frame) {
          if (neighborhood.includes(value)) {
            let target = this.nodes.get(value);
            if (!target) {
              target = new Node(value);
              this.nodes.set(value, target);
              visit.push(target);
            }

            let edge = new Edge(node, name, target);
            node.edges.push(edge);
            this.edges.push(edge);
          }
        }
      }
    }
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
    if (node.description) {
      h.push('<div class="description">');
      h.push(Component.escape(node.description));
      h.push('</div>');
    }
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
        background: #eeeeee;
      }

      $:hover {
        max-height: initial;
        background: white;
      }

      $ .title {
        font-weight: bold;
        text-align: center;
        padding-top: 2px;
      }
      $ .description {
        text-align: center;
        font-size: 14px;
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
  }

  onconnected() {
    this.attach(this.submit, "click", "#close");

    this.layout = STRAIGHT_CONNECTOR;
    this.bind("#straight", "click", e => this.onlayout(STRAIGHT_CONNECTOR));
    this.bind("#elbow", "click", e => this.onlayout(ELBOW_CONNECTOR));
    this.bind("#forked", "click", e => this.onlayout(FORK_CONNECTOR));
  }

  submit() {
    this.close();
  }

  onlayout(layout) {
    if (layout != this.layout) {
      this.layout = layout;
      this.draw();
    }
  }

  render() {
    return `
      <canvas id="canvas"></canvas>
      <div class="toolbar">
        <div class="toolbox">
          <md-icon-button icon="zoom_in"></md-icon-button>
          <md-icon-button icon="zoom_out"></md-icon-button>
          <md-icon-button icon="grid_on"></md-icon-button>
          <md-icon-button icon="grid_4x4"></md-icon-button>

          <md-icon-button
            id="straight"
            icon="straight"
            tooltip="Straight connectors">
          </md-icon-button>

          <md-icon-button
            id="elbow"
            icon="turn_right"
            tooltip="Elbow connectors">
          </md-icon-button>

          <md-icon-button
            id="forked"
            icon="merge"
            tooltip="Forked connectors">
          </md-icon-button>

          <md-icon-button
            id="close"
            icon="close"
            tooltip="Close">
          </md-icon-button>
        </div>
      </div>
    `;
  }

  onrendered() {
    // Resize canvas.
    let graph = this.state;
    let canvas = this.find("#canvas");
    canvas.width = this.scrollWidth;
    canvas.height = this.scrollHeight;

    // Assign random positions to nodes.
    let w = this.scrollWidth - MARGIN * 2 - BOX_WIDTH;
    let h = this.scrollHeight - MARGIN * 2 - BOX_HEIGHT;
    for (let n of graph.nodes.values()) {
      let box = new NodeBox(n);
      let x = Math.random() * w + MARGIN;
      let y = Math.random() * h + MARGIN;
      n.box = box;
      box.moveto(x, y);
      this.appendChild(box);
    }

    // Draw edges.
    this.draw();
  }

  draw() {
    let graph = this.state;
    let canvas = this.find("#canvas");
    let ctx = canvas.getContext("2d");

    ctx.clearRect(0, 0, canvas.width, canvas.height);
    for (let e of graph.edges) {
      let a = e.source.box.center();
      let b = e.target.box.center();

      ctx.beginPath();
      ctx.moveTo(a.x, a.y);
      if (this.layout == ELBOW_CONNECTOR && a.x != b.x && a.y != b.y) {
        if (a.y > b.y) {
          ctx.lineTo(a.x, b.y);
        } else {
          ctx.lineTo(b.x, a.y);
        }
      }
      ctx.lineTo(b.x, b.y);
      ctx.stroke();
    }
  }

  static stylesheet() {
    return `
      $ {
        position: relative;
        padding: 0px;
        width: 95vw;
        height: 95vh;
        background-color: white;
        overflow: hidden;
      }

      $ canvas {
        width: 100%;
        height: 100%;
      }

      $ .toolbar {
        position: absolute;
        top: 0;
        left: 0;
        right: 0;
        display: flex;
        justify-content: center;
      }

      $ .toolbox {
        visibility: hidden;
        display: flex;
        color: white;
        background: #808080;
        padding: 4px 12px 4px 12px;
        margin: 8px;
        border-radius: 12px;
      }

      $ .toolbar:hover .toolbox {
        visibility: initial;
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
    let topics = this.match("#editor").topics;
    let graph = new Graph(this.state, topics);

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


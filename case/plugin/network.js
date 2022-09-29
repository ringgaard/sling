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
const n_inverse_property = store.lookup("P1696");
const n_parent = store.lookup("P8810");
const n_father = store.lookup("P22");
const n_mother = store.lookup("P25");
const n_child = store.lookup("P40");
const n_spouse = store.lookup("P26");
const n_partner = store.lookup("P451");

const n_internal = store.lookup("internal");
const n_elbow = store.lookup("elbow");
const n_bundle = store.lookup("bundle");
const n_grid = store.lookup("grid");
const n_nodes = store.lookup("nodes");
const n_x = store.lookup("x");
const n_y = store.lookup("y");

const inferior = new Set([n_parent, n_father, n_mother]);
const couples = new Set([n_spouse, n_partner]);

const descriptors = [
  n_description,
  store.lookup("P19"),   // place of birth
  store.lookup("P20"),   // place of death
  store.lookup("P551"),  // residence
  store.lookup("P276"),  // location
  store.lookup("P106"),  // occupation
  n_type,
];

var inverse_cache = new Map();

function inverse(relation) {
  let inverse = inverse_cache.get(relation);
  if (inverse === undefined) {
    inverse = relation.get(n_inverse_property) || null;
    inverse_cache.set(relation, inverse);
  }
  return inverse;
}

const BOX_WIDTH = 180;
const BOX_HEIGHT = 40;
const GRID_SIZE = 20;
const MARGIN = 25;

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
    if (!this.description) {
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

  connected_to(node) {
    for (let e of this.edges) {
      if (e.source == node || e.target == node) return true;
    }
    return false;
  }

  edge(relation, target) {
    for (let e of this.edges) {
      if (e.relation == relation && e.target == target) return e;
    }
    return null;
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

  toString() {
    return `${this.source.label} --${this.relname()}--> ${this.target.label}`;
  }
}

class Bundle {
  constructor(relation) {
    this.relation = relation;
    this.edges = new Array();
    this.parents = new Set();
    this.children = new Set();
  }

  compatible(edge) {
    // Must be same relation.
    if (edge.relation != this.relation) return false;

    // All parents must be in relation to target.
    for (let p of this.parents) {
      if (!p.edge(this.relation, edge.target)) return false;
    }

    // Source must be in relation to all children.
    for (let c of this.children) {
      if (!edge.source.edge(this.relation, c)) return false;
    }

    return true;
  }

  couple(edge) {
    if (this.parents.size != 2 || this.children.size != 0) return false;
    if (!this.parents.has(edge.source)) return false;
    if (!this.parents.has(edge.target)) return false;
    return true;
  }

  add(edge) {
    this.edges.push(edge);
    this.parents.add(edge.source);
    this.children.add(edge.target);
  }

  trivial() {
    if (this.parents.size == 0 || this.children.size == 0) return true;
    return this.parents.size == 1 && this.children.size == 1;
  }

  toString() {
    let s = this.relation.get(n_name);
    for (let p of this.parents) {
      s += `  '${p.label}'`;
    }
    s += " -> ";
    for (let c of this.children) {
      s += `  '${c.label}'`;
    }
    return s;
  }
}

class Graph {
  constructor(seed, neighborhood) {
    this.nodes = new Map();
    this.edges = new Array();
    this.bundles = new Array();

    // Add seeds to graph.
    let visit = [];
    for (let topic of seed.all(n_start_point)) {
      topic = store.resolve(topic);
      if (this.nodes.has(topic)) continue;
      if (!neighborhood.includes(topic)) continue;
      let node = new Node(topic);
      this.nodes.set(topic, node);
      visit.push(node);
    }

    // Transitively traverse links from seeds.
    while (visit.length > 0) {
      let node = visit.pop();
      for (let [property, value] of node.topic) {
        value = store.resolve(value);
        if (!(value instanceof Frame)) continue;
        if (!neighborhood.includes(value)) continue;

        let target = this.nodes.get(value);
        if (!target) {
          target = new Node(value);
          this.nodes.set(value, target);
          visit.push(target);
        }

        let edge = new Edge(node, property, target);
        node.edges.push(edge);
        this.edges.push(edge);
      }
    }

    // Mark inverse relations.
    for (let e of this.edges) {
      if (!inferior.has(e.relation)) continue;
      let inverse_relation = inverse(e.relation);
      if (!inverse_relation) continue;
      let inverse_edge = e.target.edge(inverse_relation, e.source);
      if (inverse_edge) {
        e.inverse = inverse_edge;
      }
    }

    // Add bundles for couples.
    let bundles = new Array();
    for (let e of this.edges) {
      if (couples.has(e.relation)) {
        let found = false;
        for (let b of bundles) {
          if (b.couple(e)) {
            b.edges.push(e);
            found = true;
            break;
          }
        }
        if (!found) {
          let b = new Bundle(n_child);
          b.edges.push(e);
          b.parents.add(e.source);
          b.parents.add(e.target);
          bundles.push(b);
        }
      }
    }

    // Build remaining edge bundles.
    for (let n of this.nodes.values()) {
      for (let e of n.edges) {
        if (e.inverse) continue;
        if (couples.has(e.relation)) continue;
        let found = false;
        for (let b of bundles) {
          if (b.compatible(e)) {
            b.add(e);
            found = true;
            break;
          }
        }
        if (!found) {
          let b = new Bundle(e.relation);
          b.add(e);
          bundles.push(b);
        }
      }
    }

    // Only keep non-trivial bundles.
    this.bundles = new Array();
    for (let b of bundles) {
      if (b.trivial()) continue;
      for (let e of b.edges) {
        e.bundle = b;
        if (e.inverse) e.inverse.bundle = b;
      }
      this.bundles.push(b);
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

  onmouseover(e) {
    this.style.zIndex = nextz++;
    this.parentElement.active = this;
  }

  ondragstart(e) {
    this.dragx = e.x;
    this.dragy = e.y;
  }

  ondragend(e) {
    this.move(e.x - this.dragx, e.y - this.dragy, this.parentElement.grid);
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

  move(dx, dy, grid) {
    this.moveto(this.offsetLeft + dx, this.offsetTop + dy, grid);
  }

  moveto(x, y, grid) {
    if (grid) {
      x = snap(x);
      y = snap(y);
    }
    this.style.left = x + "px";
    this.style.top = y + "px";
  }

  position() {
    return new Point(this.offsetLeft, this.offsetTop);
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
  constructor(graph, network) {
    super(graph);
    this.network = network;
    this.active = null;
    this.internal = network.get(n_internal);
    if (!this.internal) this.internal = store.frame();

    this.elbow = this.internal.get(n_elbow);
    this.bundle = this.internal.get(n_bundle);
    this.grid = this.internal.get(n_grid);

    new ResizeObserver(e => {
      let canvas = this.find("#canvas");
      canvas.width = this.scrollWidth;
      canvas.height = this.scrollHeight;
      this.draw();
    }).observe(this);
  }

  onconnected() {
    this.attach(this.onkeydown, "keydown");
    this.attach(this.submit, "click", "#close");

    this.bind("#elbow", "click", e => {
      this.elbow = this.find("#elbow").toggle();
      this.draw();
    });
    this.bind("#bundle", "click", e => {
      this.bundle = this.find("#bundle").toggle();
      this.draw();
    });
    this.bind("#grid", "click", e => {
      this.grid = this.find("#grid").toggle();
      this.draw();
    });

    this.tabIndex = 1;
    this.focus();
  }

  submit() {
    // Save state.
    this.network.set(n_internal, this.internal);
    this.internal.set(n_elbow, this.elbow);
    this.internal.set(n_bundle, this.bundle);
    this.internal.set(n_grid, this.grid);
    let nodes = store.frame();
    this.internal.set(n_nodes, nodes);
    let graph = this.state;
    for (let [topic, node] of graph.nodes) {
      let p = node.box.position();
      let n = store.frame();
      n.add(n_x, p.x);
      n.add(n_y, p.y);
      nodes.add(topic, n);
    }

    this.close(true);
  }

  onkeydown(e) {
    let delta = !this.grid && e.ctrlKey ? 1 : GRID_SIZE;
    if (this.active) {
      if (e.code === "ArrowDown") {
        this.active.move(0, delta, this.grid);
        this.draw();
      } else if (e.code === "ArrowUp") {
        this.active.move(0, -delta, this.grid);
        this.draw();
      } else if (e.code === "ArrowLeft") {
        this.active.move(-delta, 0, this.grid);
        this.draw();
      } else if (e.code === "ArrowRight") {
        this.active.move(delta, 0, this.grid);
        this.draw();
      }
    }
  }

  render() {
    return `
      <canvas id="canvas"></canvas>
      <div class="toolbar">
        <div class="toolbox">
          <md-icon-toggle
            id="elbow"
            icon="turn_right"
            tooltip="Elbow connectors">
          </md-icon-toggle>

          <md-icon-toggle
            id="bundle"
            icon="merge"
            tooltip="Budle connectors">
          </md-icon-toggle>

          <md-icon-toggle
            id="grid"
            icon="grid_4x4"
            tooltip="Show/hide grid">
          </md-icon-toggle>

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

    // Update toolbox.
    this.find("#elbow").active = this.elbow;
    this.find("#bundle").active = this.bundle;
    this.find("#grid").active = this.grid;

    // Set node positions. Assign random positions to new nodes.
    let w = this.scrollWidth - MARGIN * 2 - BOX_WIDTH;
    let h = this.scrollHeight - MARGIN * 2 - BOX_HEIGHT;
    let nodes = this.internal.get(n_nodes);
    for (let [topic, node] of graph.nodes) {
      let box = new NodeBox(node);
      let x, y;
      if (nodes) {
        let n = nodes.get(topic);
        if (n) {
          x = n.get(n_x);
          y = n.get(n_y);
        }
      }

      if (!x) x = Math.random() * w + MARGIN;
      if (!y) y = Math.random() * h + MARGIN;

      node.box = box;
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

    function line(a, b) {
      ctx.beginPath();
      ctx.moveTo(a.x, a.y);
      ctx.lineTo(b.x, b.y);
      ctx.stroke();
    }

    function dot(p) {
      let r = 2;
      ctx.beginPath();
      ctx.fillRect(p.x - r, p.y - r, r * 2, r * 2);
      ctx.stroke();
    }

    function mid(a, b) {
      return new Point((a.x + b.x) / 2, (a.y + b.y) / 2);
    }

    function midpoint(nodes) {
      let count = 0;
      let x = 0;
      let y = 0;
      for (let n of nodes) {
        let p = n.box.center();
        x += p.x;
        y += p.y;
        count++;
      }
      return new Point(x / count, y / count);
    }

    function bbox(nodes) {
      let minx = Infinity;
      let maxx = 0;
      let miny = Infinity;
      let maxy = 0;
      for (let n of nodes) {
        let p = n.box.center();
        minx = Math.min(minx, p.x);
        maxx = Math.max(maxx, p.x);
        miny = Math.min(miny, p.y);
        maxy = Math.max(maxy, p.y);
      }
      return {minx, maxx, miny, maxy};
    }

    // Clear canvas.
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    // Draw grid.
    if (this.grid) {
      ctx.strokeStyle = "#eeeeee";
      for (let y = GRID_SIZE; y < canvas.height; y += GRID_SIZE) {
        line(new Point(0, y), new Point(canvas.width, y));
      }
      for (let x = GRID_SIZE; x < canvas.width; x += GRID_SIZE) {
        line(new Point(x, 0), new Point(x, canvas.height));
      }
    }

    // Draw simple edges.
    ctx.strokeStyle = "#000000";
    for (let e of graph.edges) {
      if (e.inverse) continue;
      if (this.bundle && e.bundle) continue;
      let a = e.source.box.center();
      let b = e.target.box.center();

      ctx.beginPath();
      ctx.moveTo(a.x, a.y);
      if (this.elbow && a.x != b.x && a.y != b.y) {
        if (a.y > b.y) {
          ctx.lineTo(a.x, b.y);
        } else {
          ctx.lineTo(b.x, a.y);
        }
      }
      ctx.lineTo(b.x, b.y);
      ctx.stroke();
    }

    // Draw edge bundles.
    if (this.bundle) {
      for (let b of graph.bundles) {
        let s = midpoint(b.parents)
        let d = midpoint(b.children);
        let p = mid(s, d);

        for (let parent of b.parents) line(parent.box.center(), s);
        dot(s);

        if (b.children.size == 1) {
          line(s, d);
        } else if (this.elbow) {
          let {minx, maxx, miny, maxy} = bbox(b.children);
          let w = maxx - minx;
          let h = maxy - miny;
          line(s, p);
          dot(p);
          if (h < w) {
            // Horizontal layout.
            line(new Point(minx, p.y), new Point(maxx, p.y));
            for (let child of b.children) {
              let c = child.box.center();
              let d = new Point(c.x, p.y);
              line(d, c);
              dot(d);
            }
          } else {
            // Vertial layout.
            line(new Point(p.x, miny), new Point(p.x, maxy));
            for (let child of b.children) {
              let c = child.box.center();
              let d = new Point(p.x, c.y);
              line(d, c);
              dot(d);
            }
          }
        } else {
          line(s, p);
          dot(p);
          for (let child of b.children) line(p, child.box.center());
        }
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

    let dialog = new NetworkDialog(graph, this.state);
    let updated = await dialog.show();
    if (updated) {
      this.match("topic-card").mark_dirty();
    }
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


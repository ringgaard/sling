// Copyright 2026 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for family tree.

import {store, frame} from "/common/lib/global.js";
import {Component, html} from "/common/lib/component.js";
import {MdDialog} from "/common/lib/material.js";
import {ItemCollector, Time} from "/common/lib/datatype.js";

const n_id = frame("id")
const n_is = frame("is");
const n_name = frame("name");
const n_description = frame("description");
const n_type = frame("P31");
const n_human = frame("Q5");
const n_gender = frame("P21");
const n_male = frame("Q6581097");
const n_female = frame("Q6581072");
const n_born = frame("P569");
const n_died = frame("P570");

const n_parent = frame("P8810");
const n_father = frame("P22");
const n_mother = frame("P25");
const n_child = frame("P40");
const n_spouse = frame("P26");
const n_partner = frame("P451");

const top_spacing = 25;
const box_width = 250;
const box_height = 50;
const box_spacing = 25;
const lane_spacing = 15;
const name_xofs = 10;
const name_yofs = 20;
const max_delta = 500;
const delta_limit = 50;
const layout_iterations = 20;

function date(d) {
  let t = new Time(store.resolve(d));
  if (t.precision > 4) t.precision = 4;
  return t.text();
}

class Person {
  constructor(topic) {
    this.topic = topic;
    let redir = topic.get(n_is);
    this.base = redir && frame(redir);
    this.parents = [];
    this.children = [];
    this.partners = [];
    this.generation = 0;
  }

  id() {
    return this.topic.id;
  }

  name() {
    return (this.property(n_name) || this.topic.id).toString();
  }

  property(prop) {
    let value = store.resolve(this.topic.get(prop));
    if (!value && this.base) value = store.resolve(this.base.get(prop));
    return value;
  }

  description() {
    let s = "";
    let gender = this.property(n_gender);
    let born = this.property(n_born);
    let died = this.property(n_died);
    if (gender == n_male) s += "♂ ";
    if (gender == n_female) s += "♀ ";
    if (born) s += "* " + date(born) + " ";
    if (died) s += "† " + date(died) + " ";
    if (this.distance) s += " [" + this.distance + "]";
    return s.trim();
  }

  relative(kinship) {
    let p = this.topic.get(kinship);
    if (!p) p = this.base?.get(kinship);
    p = store.resolve(p);
    if (typeof(p) === 'string') return;
    return p;
  }

  relatives(kinship) {
    let it = function* (topic, base) {
      for (let r of topic.all(kinship)) {
       r = store.resolve(r);
       if (typeof(r) === 'string') continue;
        yield r;
      }
      if (base) {
        for (let r of base.all(kinship)) {
          r = store.resolve(r);
          if (typeof(r) === 'string') continue;
          yield r;
        }
      }
    };
    return it(this.topic, this.base);
  }

  add_parent(person) {
    if (!this.parents.includes(person)) this.parents.push(person);
  }

  add_partner(person) {
    if (!this.partners.includes(person)) this.partners.push(person);
  }

  add_child(person) {
    if (!this.children.includes(person)) this.children.push(person);
  }

  dob() {
    return new Time(this.property(n_born));
  }

  issibling(other) {
    if (this.parents.length != other.parents.length) return false;
    for (let parent of this.parents) {
      if (!other.parents.includes(parent)) return false;
    }
    return this.parents.length > 0;
  }

  ispartner(other) {
    return this.partners.includes(other);
  }

  toString() {
    let h = [];
    h.push(`gen ${this.generation} person ${this.name()} (${this.id()})`);
    for (let p of this.parents) h.push(`  parent ${p.name()} (${p.id()})`);
    for (let p of this.partners) h.push(`  partner ${p.name()} (${p.id()})`);
    for (let c of this.children) h.push(`  child ${c.name()} (${c.id()})`);
    return h.join("\n");
  }
}

function family_key(parents) {
  let ids = new Array();
  for (let p of parents) ids.push(p.id());
  if (ids.length == 0) return undefined;
  ids.sort();
  return ids.join(":");
}

class Family {
  constructor() {
    this.parents = [];
    this.children = [];
  }

  add_parent(person) {
    if (!this.parents.includes(person)) this.parents.push(person);
  }

  add_child(person) {
    if (!this.children.includes(person)) this.children.push(person);
  }

  parent_center() {
    if (this.parents.length == 0) return;
    let sum = 0;
    for (let p of this.parents) sum += p.y;
    return sum / this.parents.length;
  }

  child_center() {
    if (this.children.length == 0) return;
    let sum = 0;
    for (let c of this.children) sum += c.y;
    return sum / this.children.length;
  }

  toString() {
    let h = [];
    h.push(`family ${family_key(this.parents)}`);
    for (let p of this.parents) h.push(`  parent ${p.name()} (${p.id()})`);
    for (let c of this.children) h.push(`  child ${c.name()} (${c.id()})`);
    return h.join("\n");
  }
}

class Generation {
  constructor(index) {
    this.index = index;
    this.lanes = 0;
    this.persons = [];
    this.families = [];
  }

  add_person(person) {
    if (!this.persons.includes(person)) {
      this.persons.push(person);
    }
  }

  add_family(family) {
    if (!this.families.includes(family)) {
      this.families.push(family);
    }
  }

  adjacent(person1, person2) {
    if (!person1 || !person2) return false;
    if (person1.index == person2.index + 1) return true;
    if (person1.index == person2.index - 1) return true;
    return false;
  }
}

class FamilyTree {
  constructor(mingen, maxgen, index) {
    this.index = index;
    this.persons = new Map();
    this.families = new Map();
    this.generations = new Map();
    this.radius = (maxgen - mingen + 1) * 2;
    for (let g = mingen; g <= maxgen; ++g) {
      this.generations.set(g, new Generation(g));
    }
  }

  async build(topic) {
    await this.traverse(topic);
    this.order();
    this.layout();
  }

  family(key) {
    if (key === undefined) return undefined;
    let f = this.families.get(key);
    if (!f) {
      f = new Family();
      this.families.set(key, f);
    }
    return f;
  }

  person(topic) {
    let p = this.persons.get(topic);
    if (p) return p;

    if (this.index) {
      topic = this.index.ids.get(topic.id) || topic;
      p = this.persons.get(topic);
      if (p) return p;
    }

    p = new Person(topic);
    this.persons.set(topic, p);
    if (p.base) this.persons.set(p.base, p);
    return p;
  }

  async traverse(topic) {
    // Build familiy tree by following parent/child/partner relations.
    let queue = [];
    let visited = new Set();
    this.subject = this.person(topic);
    this.subject.distance = 1;
    queue.push(this.subject);
    let i = 0;
    while (i < queue.length) {
      if (i > 1000) {
        console.log("family tree too big", this);
        break;
      }

      // Collect external items.
      let collector = new ItemCollector(store);
      for (let j = i; j < queue.length; ++j) {
        collector.add(queue[j].topic);
        collector.add(queue[j].base);
      }
      await collector.retrieve();

      // Get next person.
      let person = queue[i++];
      let generation = this.generations.get(person.generation);
      if (!generation) {
        console.log("generations exceeded", person.id(), person.name(), person);
        continue;
      }
      generation.add_person(person);
      if (visited.has(person)) continue;
      if (person.distance == this.radius) continue;
      visited.add(person);

      // Find parents.
      if (this.generations.has(person.generation - 1)) {
        let father = store.resolve(person.relative(n_father));
        if (father) {
          let f = this.person(father);
          f.generation = person.generation - 1;
          if (!f.distance) f.distance = person.distance + 1;
          person.add_parent(f);
          if (!visited.has(f)) queue.push(f);
        }
        let mother = store.resolve(person.relative(n_mother));
        if (mother) {
          let m = this.person(mother);
          m.generation = person.generation - 1;
          if (!m.distance) m.distance = person.distance + 1;
          person.add_parent(m);
          if (!visited.has(m)) queue.push(m);
        }
      }

      // Find spouses.
      for (let spouse of person.relatives(n_spouse)) {
        let s = this.person(store.resolve(spouse));
        s.generation = person.generation;
        if (!s.distance) s.distance = person.distance + 1;
        person.add_partner(s);
        if (!visited.has(s)) queue.push(s);
      }

      // Find children.
      if (this.generations.has(person.generation + 1)) {
        for (let child of person.relatives(n_child)) {
          let c = this.person(store.resolve(child));
          c.generation = person.generation + 1;
          if (!c.distance) c.distance = person.distance + 1;
          person.add_child(c);
          if (!visited.has(c)) queue.push(c);
        }
      }
    }

    // Build families with parents and children.
    let persons = new Set(this.persons.values());
    for (let person of persons) {
      let family = this.family(family_key(person.parents));
      if (family) {
        family.generation = person.generation - 1;
        for (let parent of person.parents) family.add_parent(parent);
        family.add_child(person);
      }
      for (let partner of person.partners) {
        let family = this.family(family_key([person, partner]));
        if (family) {
          family.generation = person.generation;
          family.add_parent(person);
          family.add_parent(partner);
        }
      }
    }

    // Add families to generations.
    for (let family of this.families.values()) {
      if (this.generations.has(family.generation)) {
        this.generations.get(family.generation).add_family(family);
      } else {
        console.log("family generation exceeded", family);
      }
    }
    for (let generation of this.generations.values()) {
      let lane = 1;
      for (let family of generation.families) {
        family.lane = lane++;
      }
    }
  }

  order() {
    for (let generation of this.generations.values()) {
      //console.log("reorder generation " + generation.index);

      // Sort persons in generation by age.
      let g = generation.persons;
      g.sort((a, b) => {
        let dob_a = a.dob().number();
        let dob_b = b.dob().number();
        if (!dob_a && !dob_b) return 0;
        if (!dob_a) return 1;
        if (!dob_b) return -1;
        return dob_a - dob_b;
      });

      // Move partners and siblings together.
      for (let i = 0; i < g.length; ++i) {
        //let s = [];
        //for (let p of g) s.push(p.name());
        //console.log(s);

        let insert = i + 1;
        for (let j = i + 1; j < g.length; ++j) {
          if (g[i].ispartner(g[j])) {
            let partner = g[j];
            //console.log(`move ${g[i].name()} partner ${partner.name()} from ${j} to ${insert}`);
            g.splice(j, 1);
            g.splice(insert++, 0, partner);
          }
        }

        for (let j = i + 1; j < g.length; ++j) {
          if (g[i].issibling(g[j])) {
            let sibling = g[j];
            //console.log(`move ${g[i].name()} sibling ${sibling.name()} from ${j} to ${insert}`);
            g.splice(j, 1);
            g.splice(insert++, 0, sibling);
          }
        }
      }
    }
  }

  layout() {
    function first(persons) {
      if (persons.length == 0) return;
      let low = persons[0].index;
      for (let p of persons) {
        if (p.index < low) low = p.index;
      }
      return low;
    }

    function together(family, generation) {
      if (family.parents.length == 1) return true;
      if (family.parents.length == 2) {
        return generation.adjacent(family.parents[0], family.parents[1]);
      }
      return false;
    }

    // Initial grid layout.
    let x = top_spacing;
    for (let generation of this.generations.values()) {
      if (generation.persons.length == 0) continue;

      let y = top_spacing;
      let index = 0;
      for (let person of generation.persons) {
        person.x = x;
        person.y = y;
        person.index = index++;
        y += box_height + box_spacing;
      }

      // TODO: assign lanes to families based on overlap
      generation.lanes = generation.families.length;
      generation.x = x;

      x += box_width + box_spacing + generation.lanes * lane_spacing;
    }

    for (let iteration = 0; iteration < layout_iterations; iteration++) {
      let converged = true;
      console.log("align iter", iteration);
      // Align parents with children.
      for (let generation of this.generations.values()) {
        for (let family of generation.families) {
          if (together(family, generation)) {
            let midp = family.parent_center();
            let midc = family.child_center();
            //console.log("midp", midp, "midc", midc);
            if (midp && midc) {
              let delta = midc - midp;
              if (delta > 0 && delta < max_delta) {
                if (delta > delta_limit) delta = delta_limit;
                let from = first(family.parents);
                console.log("parent delta", delta, "family", family.parents[0].name(), "from", from);
                for (let i = from; i < generation.persons.length; ++i) {
                  generation.persons[i].y += delta;
                }
                converged = false;
              }
            }
          }
        }
      }

      // Align children with parents.
      let prev;
      for (let generation of this.generations.values()) {
        if (prev) {
          for (let family of prev.families) {
            let midp = family.parent_center();
            let midc = family.child_center();
            if (midp && midc) {
              let delta = midp - midc;
              if (delta > 0 && delta < max_delta) {
                if (delta > delta_limit) delta = delta_limit;
                let from = first(family.children);
                console.log("child delta", delta, "family", family.parents[0].name());
                for (let i = from; i < generation.persons.length; ++i) {
                  generation.persons[i].y += delta;
                }
                converged = false;
              }
            }
          }
        }
        prev = generation;
      }

      // Stop if layout has converged.
      if (converged) break;
    }
  }

  bounds() {
    let width = 0;
    let height = 0;
    let last = undefined;
    for (let generation of this.generations.values()) {
      if (generation.persons.length == 0) continue;
      for (let person of generation.persons) {
        if (person.x > width) width = person.x;
        if (person.y > height) height = person.y;
      }
      last = generation;
    }
    width += box_width + box_spacing;
    height += box_height + box_spacing;
    if (last) {
      width += last.lanes * lane_spacing;
    }
    return [width, height];
  }
}

class FamilyTreeDialog extends MdDialog {
  onrendered() {
    this.attach(this.onclose, "click", "#close");
    this.attach(this.onclick, "click", "#chart");
  }

  onclick(e) {
    let ref = e.target.getAttribute("ref");
    if (ref) this.close(ref);
  }

  onclose(e) {
    this.close();
  }

  render() {
    let tree = this.state;
    let name = tree.subject.name();
    let h = html`<md-dialog-top>Family tree for ${name}</md-dialog-top>`;
    let [width, height] = tree.bounds();

    h.html`<div id=tree><svg id=chart
           width=${width} height=${height}
           viewBox="0 0 ${width} ${height}"
           preserveAspectRatio="xMidYMid"
           xmlns="http://www.w3.org/2000"/svg">`;
    for (let generation of tree.generations.values()) {
      for (let f of generation.families) {
        let lx = generation.x + box_width + f.lane * lane_spacing;
        let ly0 = undefined;
        let ly1 = undefined;

        if (f.parents.length == 2 &&
            generation.adjacent(f.parents[0], f.parents[1])) {
            let x = f.parents[0].x + box_width / 2;
            let y0 = f.parents[0].y;
            let y1 = f.parents[1].y;
            let ymid = (y0 + box_height + y1) / 2;
            if (!ly0 || ymid < ly0) ly0 = ymid;
            if (!ly1 || ymid > ly1) ly1 = ymid;
            h.html`<line x1="${x}" y1="${y0}" x2="${x}" y2="${y1}"/>`;
            if (f.children.length > 0) {
              h.html`<line x1="${x}" y1="${ymid}" x2="${lx}" y2="${ymid}"/>`;
              h.html`<circle cx="${lx}" cy="${ymid}" r="3"/>`;
            }
        } else {
          for (let p of f.parents) {
            if (p.x === undefined || p.y === undefined) {
              console.log("no position", p);
              continue;
            }
            let x = p.x + box_width;
            let y = p.y + box_height / 2;
            if (!ly0 || y < ly0) ly0 = y;
            if (!ly1 || y > ly1) ly1 = y;
            h.html`<line x1="${x}" y1="${y}" x2="${lx}" y2="${y}"/>`;
            h.html`<circle cx="${lx}" cy="${y}" r="3"/>`;
          }
        }
        for (let c of f.children) {
          let x = c.x;
          let y = c.y + box_height / 2;
          if (!ly0 || y < ly0) ly0 = y;
          if (!ly1 || y > ly1) ly1 = y;
          h.html`<line x1="${x}" y1="${y}" x2="${lx}" y2="${y}"/>`;
          h.html`<circle cx="${lx}" cy="${y}" r="3"/>`;
        }
        if (ly0 != ly1) {
          h.html`<line x1="${lx}" y1="${ly0}" x2="${lx}" y2="${ly1}"/>`;
        }
      }
      for (let p of generation.persons) {
        h.html`<rect ref="${p.id()}" x="${p.x}" y="${p.y}"
                width="${box_width}" height="${box_height}"
                rx="10" ry="10"/>
               <text class="name"
                 x="${p.x + box_width / 2}" y="${p.y + name_yofs}">
                 ${p.name()}
               </text>
               <text class="descr"
                 x="${p.x + box_width / 2}" y="${p.y + name_yofs * 2}">
                 ${p.description()}
               </text>`;
      }
    }
    h.html`</svg></div>`;

    h.html`
      <div class="toolbar">
        <div class="toolbox">
          <md-icon-button
            id="ancestors"
            icon="fast_rewind">
          </md-icon-button>
          <md-icon-button
            id="descendants"
            icon="fast_forward">
          </md-icon-button>
          <md-icon-button
            id="close"
            icon="close"
            tooltip-align="top">
          </md-icon-button>
        </div>
      </div>`;
    return h;
  }

  static stylesheet() {
    return `
      $ {
        padding-bottom: 16px;
      }
      $ div#tree {
        width: calc(100vw * 0.95);
        height: calc(100vh * 0.85);
        overflow: auto;
        outline: none;
      }
      $ svg {
        /*width: auto;*/
        /*height: auto;*/
      }
      $ rect {
        stroke: #cccccc;
        stroke-width: 2px;
        fill: white;
      }
      $ rect:hover {
        fill: #cccccc;
        cursor: pointer;
      }
      $ line {
        stroke: #cccccc;
        stroke-width: 2px;
      }
      $ text {
        /*dominant-baseline: middle;*/
        text-anchor: middle;
      }
      $ text.name {
        font-size: 14px;
        font-weight: bold;
      }
      $ text.descr {
        font-size: 12px;
      }
      $ circle {
        fill: #cccccc;
      }
      $ .toolbar {
        position: absolute;
        bottom: 0;
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
        margin: 12px;
        border-radius: 12px;
      }

      $ .toolbar:hover .toolbox {
        visibility: initial;
      }
    `;
  }
}

Component.register(FamilyTreeDialog);

export default class FamilyTreePlugin {
  async run(topic, index) {
    console.log("family tree for " + topic.id);
    let tree = new FamilyTree(-1, 1, index);
    await tree.build(topic);

    let dialog = new FamilyTreeDialog(tree)
    return await dialog.show();
  }
}

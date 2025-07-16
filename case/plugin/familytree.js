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
const n_kinship = frame("P1039");

const top_spacing = 25;
const box_width = 250;
const box_height = 50;
const box_spacing = 25;
const lane_spacing = 15;
const name_xofs = 10;
const name_yofs = 20;
const layout_iterations = 12;
const max_name_length = 35;

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
    let name = this.property(n_name)
    if (!name) return id();
    name = name.toString();
    if (name.length > max_name_length) {
      // Remove titles.
      let comma = name.indexOf(",");
      if (comma != -1) name = name.slice(0, comma);

      if (name.length > max_name_length) {
        // Shorten name by replacing names with initials.
        let len = name.length;
        let parts = name.split(" ");
        for (let i = 0; i < parts.length; ++i) {
          len -= parts[i].length - 1;
          parts[i] = parts[i][0];
          if (len < max_name_length) break;
        }
        name = parts.join(" ");
      }
    }
    return name;
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
        let f = store.resolve(r);
        if (typeof(f) === 'string') continue;
        if (f != r && r.has(n_kinship)) continue;
        yield f;
      }
      if (base) {
        for (let r of base.all(kinship)) {
          let f = store.resolve(r);
          if (typeof(f) === 'string') continue;
          if (f != r && r.has(n_kinship)) continue;
          yield f;
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

  mid_parent() {
    if (this.parents.length == 0) return;
    let sum = 0;
    for (let p of this.parents) sum += p.index;
    return sum / this.parents.length;
  }

  mid_child() {
    if (this.children.length == 0) return;
    let sum = 0;
    for (let c of this.children) sum += c.index;
    return sum / this.children.length;
  }

  nucleus() {
    if (this.children.length == 0) return false;
    return this.parents.length == 1 || this.parents.length == 2;
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

  swap(index1, index2) {
    let person1 = this.persons[index1];
    let person2 = this.persons[index2];
    this.persons[index1] = person2;
    this.persons[index2] = person1;
    person1.index = index2;
    person2.index = index1;
  }
}

class FamilyTree {
  constructor(mingen, maxgen, radius, index) {
    this.index = index;
    this.persons = new Map();
    this.families = new Map();
    this.mingen = mingen;
    this.maxgen = maxgen;
    this.radius = radius;
    this.generations = new Map();
    for (let g = mingen; g <= maxgen; ++g) {
      this.generations.set(g, new Generation(g));
    }
    console.log(`gen ${mingen} - ${maxgen} radius: ${radius}`);
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
    this.subject.distance = 0;
    queue.push(this.subject);
    let i = 0;
    while (i < queue.length) {
      if (i > 1000) {
        console.log("family tree too big", this);
        this.overflow = true;
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
      visited.add(person);

      // Find parents.
      if (this.generations.has(person.generation - 1)) {
        let father = store.resolve(person.relative(n_father));
        if (father) {
          let f = this.person(father);
          f.generation = person.generation - 1;
          if (f.distance == undefined) f.distance = person.distance;
          person.add_parent(f);
          if (!visited.has(f)) queue.push(f);
        }
        let mother = store.resolve(person.relative(n_mother));
        if (mother) {
          let m = this.person(mother);
          m.generation = person.generation - 1;
          if (m.distance == undefined) m.distance = person.distance;
          person.add_parent(m);
          if (!visited.has(m)) queue.push(m);
        }
      }

      // Find spouses.
      if (person.distance < this.radius) {
        for (let spouse of person.relatives(n_spouse)) {
          let s = this.person(store.resolve(spouse));
          s.generation = person.generation;
          if (s.distance == undefined) s.distance = person.distance + 1;
          person.add_partner(s);
          if (!visited.has(s)) queue.push(s);
        }
      }

      // Find children.
      if (this.generations.has(person.generation + 1)) {
        for (let child of person.relatives(n_child)) {
          let c = this.person(store.resolve(child));
          c.generation = person.generation + 1;
          if (c.distance == undefined) c.distance = person.distance;
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
        let insert = i + 1;
        for (let j = i + 1; j < g.length; ++j) {
          if (g[i].ispartner(g[j])) {
            let partner = g[j];
            g.splice(j, 1);
            g.splice(insert++, 0, partner);
          }
        }

        for (let j = i + 1; j < g.length; ++j) {
          if (g[i].issibling(g[j])) {
            let sibling = g[j];
            g.splice(j, 1);
            g.splice(insert++, 0, sibling);
          }
        }
      }

      for (let index = 0; index < g.length; ++index) {
        g[index].index = index;
      }
    }

    // Swap parent order based on children.
    for (let generation of this.generations.values()) {
      for (let i = 0; i <  generation.families.length; ++i) {
        let f1 = generation.families[i];
        if (!f1.nucleus()) continue;

        for (let j = i + 1; j <  generation.families.length; ++j) {
          let f2 = generation.families[j];
          if (!f2.nucleus()) continue;
          if (f1 == f2) continue;
          if (f1.parents.length != f2.parents.length) continue;

          let mp1 = f1.mid_parent();
          let mp2 = f2.mid_parent();
          let mc1 = f1.mid_child();
          let mc2 = f2.mid_child();
          if (mp1 < mp2 && mc1 > mc2) {
            console.log(`swap family ${f1.parents[0].name()} with ${f2.parents[0].name()}`);
            for (let k = 0; k < f1.parents.length; ++k) {
              generation.swap(f1.parents[k].index, f2.parents[k].index);
            }
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

    function min(a, b) {
      if (a == undefined) return b;
      if (b == undefined) return a;
      return a < b ? a : b;
    }

    function move(persons, start, offset) {
      for (let i = start; i < persons.length; ++i) {
        persons[i].y += offset;
      }
    }

    // Initial grid layout.
    let x = top_spacing;
    for (let generation of this.generations.values()) {
      if (generation.persons.length == 0) continue;

      let y = top_spacing;
      for (let person of generation.persons) {
        person.x = x;
        person.y = y;
        y += box_height + box_spacing;
      }

      // TODO: assign lanes to families based on overlap
      generation.lanes = generation.families.length;
      generation.x = x;

      x += box_width + box_spacing + generation.lanes * lane_spacing;
    }

    // Align parents and children.
    for (let iteration = 0; iteration < layout_iterations; iteration++) {
      let converged = true;
      // Align parents with children.
      for (let generation of this.generations.values()) {
        for (let family of generation.families) {
          if (together(family, generation)) {
            let midp = family.parent_center();
            let midc = family.child_center();
            if (midp && midc) {
              let delta = midc - midp;
              if (delta > 0) {
                let from = first(family.parents);
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
              if (delta > 0) {
                let from = first(family.children);
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

    // Tree compaction.
    let gen = [];
    let pos = [];
    for (let generation of this.generations.values()) {
      gen.push(generation.persons);
      pos.push(0);
    }

    // Remove top gap.
    let top_margin;
    for (let g of gen) {
      if (g.length > 0) top_margin = min(top_margin,  g[0].y);
    }
    if (top_margin > top_spacing) {
      for (let g of gen) move(g, 0, top_spacing - top_margin);
    }

    // Remove internal gaps.
    for (;;) {
      // Find minimum y position.
      let y;
      for (let i = 0; i < gen.length; ++i) {
        if (pos[i] < gen[i].length) {
          let p = gen[i][pos[i]];
          y = min(y, p.y);
        }
      }
      if (y === undefined) break;

      // Compute gap to next.
      let gap;
      for (let i = 0; i < gen.length; ++i) {
        if (pos[i] < gen[i].length) {
          let p = gen[i][pos[i]];
          if (p.y == y) {
            pos[i]++;
            if (pos[i] < gen[i].length) {
              let q = gen[i][pos[i]];
              let spacing = q.y - y;
              gap = min(gap, spacing);
            }

          } else {
            gap = min(gap, p.y - y);
          }
        }
      }
      if (gap > box_height + box_spacing) {
        let adjust = gap - box_height - box_spacing;
        for (let i = 0; i < gen.length; ++i) {
          if (pos[i] < gen[i].length) {
            move(gen[i], pos[i], -adjust);
          }
        }
      }
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
    this.attach(this.onclick, "click", "#chart");
    this.attach(this.onclose, "click", "#close");
    this.attach(this.onchange, "click", "#ancestors");
    this.attach(this.onchange, "click", "#descendants");
    this.attach(this.onchange, "click", "#radius");

  }
  onclick(e) {
    let ref = e.target.getAttribute("ref");
    if (ref) this.close(ref);
  }

  onclose(e) {
    this.close();
  }

  async onchange(e) {
    let t = this.state;
    let mingen = -this.find("#ancestors").value();
    let maxgen = this.find("#descendants").value();
    let radius = this.find("#radius").value();
    let tree = new FamilyTree(mingen, maxgen, radius, t.index);
    await tree.build(t.subject.topic);
    this.update(tree);
  }

  render() {
    let tree = this.state;
    let name = tree.subject.name();
    let h = html`<md-dialog-top>
      <span class="title">${name} family</span>
      <md-slider id="ancestors"
                 min="0" max="9" step="1" value="${-tree.mingen}"
                 label="Ancestors">
      </md-slider>
      <md-slider id="descendants"
                 min="0" max="9" step="1" value="${tree.maxgen}"
                 label="Descendants">
      </md-slider>
      <md-slider id="radius"
                 min="0" max="9" step="1" value="${tree.radius}"
                 label="Radius">
      </md-slider>
      <span class="icons">
        <md-icon-button id="zoomin" icon="zoom_in"></md-icon-button>
        <md-icon-button id="zoomout" icon="zoom_out"></md-icon-button>
        <md-icon-button id="close" icon="close"></md-icon-button>
      </span>
    </md-dialog-top>`;
    let [width, height] = tree.bounds();
    if (tree.overflow) h.html`<p class="alert">overflow</p>`;

    h.html`<div id=tree><svg id=chart
           width="${width}" height="${height}"
           viewBox="0 0 ${width} ${height}"
           preserveAspectRatio="xMidYMid"
           xmlns="http://www.w3.org/2000/svg">`;
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
                rx="10" ry="10"
                ${p == tree.subject ? 'class=subject' : ''} />
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
      $ md-dialog-top {
        display: flex;
        justify-content: space-between;
        gap: 16px;
      }
      $ span.title {
        flex: 1 0;
      }
      $ span.icons {
        display: flex;
        flex: 0;
      }
      $ md-slider {
        flex: 1 1;
      }
      $ svg {
        /*width: auto;*/
        /*height: auto;*/
      }
      $ p.alert {
        color: red;
      }
      $ rect {
        stroke: #cccccc;
        stroke-width: 2px;
        fill: white;
      }
      $ rect.subject {
        stroke: black;
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
    `;
  }
}

Component.register(FamilyTreeDialog);

export default class FamilyTreePlugin {
  async run(topic, index) {
    let tree = new FamilyTree(-1, 1, 1, index);
    await tree.build(topic);

    let dialog = new FamilyTreeDialog(tree)
    return await dialog.show();
  }
}

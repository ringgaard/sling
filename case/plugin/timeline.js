// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING topic widget for displaying timelines.

import {Component} from "/common/lib/component.js";
import {MdDialog} from "/common/lib/material.js";
import {store, settings} from "/case/app/global.js";
import {ItemCollector, LabelCollector} from "/case/app/value.js";
import {qualified} from "/case/app/schema.js";

const n_name = store.lookup("name");
const n_depicts = store.lookup("P180");
const n_type = store.lookup("P31");
const n_target = store.lookup("target");
const n_item_type = store.lookup("/w/item");
const n_point_in_time = store.lookup("P585");
const n_start_time = store.lookup("P580");
const n_end_time = store.lookup("P582");
const n_inception = store.lookup("P571");
const n_dissolved = store.lookup("P576");
const n_birth_date = store.lookup("P569");
const n_death_date = store.lookup("P570");


const discarded_topic_types = [
  store.lookup("Q108673968"), // case file
  store.lookup("Q186117"),    // time line
];

function range(topic) {
  let begin = topic.get(n_point_in_time);
  let end = begin;

  if (!begin) begin = topic.get(n_start_time);
  if (!begin) begin = topic.get(n_birth_date);
  if (!begin) begin = topic.get(n_inception);
  if (begin) begin = store.resolve(begin);

  if (!end) end = topic.get(n_end_time);
  if (!end) end = topic.get(n_death_date);
  if (!end) end = topic.get(n_dissolved);
  if (end) end = store.resolve(end);

  return [begin, end];
}

function name(item) {
  let name = item && item.get && (item.get(n_name) || item.id) || item;
  if (name) name = name.toString();
  return name;
}

class TimelineDialog extends MdDialog {
  async init(topics) {
    // Get all topics for chart.
    this.sections = new Array();
    let collector = new LabelCollector(store);
    for (let topic of topics) {
      let begin, end;

      // Get events for topic.
      let events = new Array();
      for (let [prop, value] of topic) {
        if (!qualified(value)) continue;
        if (prop.get(n_target) != n_item_type) continue;
        let [begin, end] = range(value);
        if (begin || end) {
          value = store.resolve(value);
          events.push({topic, prop, value, begin, end});
          collector.add_item(value);
        }
      }

      for (let link of topic.links(true)) {
        let [b, e] = range(link);
        begin = begin || b;
        end = end || e;
        for (let [prop, value] of link) {
          if (!qualified(value)) continue;
          if (prop.get(n_target) != n_item_type) continue;
          let [begin, end] = range(value);
          if (begin || end) {
            value = store.resolve(value);
            events.push({topic, prop, value, begin, end});
            collector.add_item(value);
          }
        }
      }

      await collector.retrieve();

      if (begin || end || events.length > 0) {
        this.sections.push({topic, begin, end, events});
      }
    }

    for (let s of this.sections) {
      console.log(s.topic.id, name(s.topic), s.begin, s.end);
      for (let e of s.events) {
        console.log("  event", name(e.prop), name(e.value), e.begin, e.end);
      }
    }
  }

  render() {
    return `
      <div id="timeline">Timeline</div>
    `;
  }
}

Component.register(TimelineDialog);

export default class TimelineWidget extends Component {
  onconnected() {
    this.attach(this.onclick, "click");
  }

  async onclick(e) {
    // Get topics for timeline.
    let widget = this.state;
    let depicts = widget.has(n_depicts) && widget.all(n_depicts);
    if (!depicts) depicts = this.match("#editor").topics;

    let topics = new Array();
    let collector = new ItemCollector(store);
    for (let topic of depicts) {
      if (discarded_topic_types.includes(topic.get(n_type))) continue;
      topics.push(topic);
      collector.add(topic);
      collector.add_links(topic);
    }
    await collector.retrieve();

    // Show timeline dialog.
    let dialog = new TimelineDialog(this.state);
    await dialog.init(topics);
    let updated = await dialog.show();
    if (updated) {
      this.match("topic-card").mark_dirty();
    }
  }

  render() {
    return `<md-button label="Show timeline"></md-button>`;
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

Component.register(TimelineWidget);


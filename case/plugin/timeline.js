// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING topic widget for displaying timelines.

import {Component} from "/common/lib/component.js";
import {MdDialog} from "/common/lib/material.js";
import {store, frame, settings} from "/case/app/global.js";
import {ItemCollector, LabelCollector} from "/case/app/value.js";
import {qualified} from "/case/app/schema.js";

const n_name = frame("name");
const n_description = frame("description");
const n_depicts = frame("P180");
const n_type = frame("P31");
const n_target = frame("target");
const n_item_type = frame("/w/item");
const n_point_in_time = frame("P585");
const n_start_time = frame("P580");
const n_end_time = frame("P582");
const n_inception = frame("P571");
const n_dissolved = frame("P576");
const n_birth_date = frame("P569");
const n_death_date = frame("P570");


const discarded_topic_types = [
  frame("Q108673968"), // case file
  frame("Q186117"),    // time line
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
  }

  onconnected() {
    this.bind("#close", "click", e => this.cancel());
  }

  render() {
    let widget = this.state;
    let h = "";
    h += '<md-icon-button id="close" icon="close"></md-icon-button>';

    let title = widget.get(n_name);
    let subtitle = widget.get(n_description);
    if (title || subtitle) {
      h += '<div class="titlebar">';
      if (title) {
        h += `<div class="title">${Component.escape(title)}</div>`;
      }
      if (subtitle) {
        h += `<div class="subtitle">${Component.escape(subtitle)}</div>`;
      }
      h += '</div>';
    }

    h += '<div class="container">';
    h += '<table>';
    for (let s of this.sections) {
      h += `<tr>
        <td class="sn">${name(s.topic)}</td>
        <td class="sv">${s.begin} ${s.end}</td>
      </tr>`;
      for (let e of s.events) {
        h += `<tr>
          <td class="en">${name(e.prop)}: ${name(e.value)}</td>
          <td class="ev">${e.begin} ${e.end}</td>
        </tr>`;
      }
    }
    h += '</table>';
    h += '</div>';
    return h;
  }

  static stylesheet() {
    return `
      $ {
        position: relative;
        width: 95vw;
        height: 95vh;
        padding: 0;
      }

      $ #close {
        position: absolute;
        top: 0;
        right: 0;
        padding: 4px 4px;
      }

      $ .titlebar {
        padding: 8px;
      }

      $ .title {
        font-size: 24px;
        font-weight: 500;
        text-align: center;
        display: flex;
        justify-content: center;
      }

      $ .subtitle {
        display: flex;
        justify-content: center;
        font-size: 16px;
      }

      $ .container {
        position: relative;
        height: 100%;
        overflow: auto;
        margin-bottom: 8px;
        white-space: nowrap;
      }

      $ .container table {
        table-layout: fixed;
        width: 100%;
      }

      $ .sn {
        position: sticky;
        left: 0px;
        padding-left: 8px;
        font-weight: bold;
        width: 30%;
        overflow: hidden;
        white-space: nowrap;
        text-overflow: ellipsis;
        background-color: white;
      }

      $ .sv {
      }

      $ .en {
        position: sticky;
        left: 0px;
        padding-left: 16px;
        width: 30%;
        overflow: hidden;
        white-space: nowrap;
        text-overflow: ellipsis;
        background-color: white;
      }

      $ .ev {
      }
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


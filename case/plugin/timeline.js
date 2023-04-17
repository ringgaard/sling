// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING topic widget for displaying timelines.

import {Component} from "/common/lib/component.js";
import {MdDialog} from "/common/lib/material.js";
import {store, frame, settings} from "/common/lib/global.js";
import {ItemCollector, LabelCollector, Time} from "/common/lib/datatype.js";

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

const event_colors = new Map([
  [frame("P451"), "pink"],       // unmarried partner
  [frame("P26"), "red"],         // spouse

  [frame("P69"), "lightblue"],   // educated at
  [frame("P108"), "skyblue"],    // employer
  [frame("P39"), "skyblue"],     // position held

  [frame("P169"), "steelblue"],  // chief executive officer
  [frame("P2828"), "steelblue"], // corporate officer
  [frame("P488"), "steelblue"],  // chairperson
  [frame("P3320"), "steelblue"], // board member

  [frame("P127"), "royalblue"],  // owned by

  [frame("P551"), "green"],      // residence
  [frame("P27"), "green"],       // country of citizenship
  [frame("P159"), "green"],      // headquarters location
]);

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

  return [
    begin ? new Time(begin) : undefined,
    end ? new Time(end) : undefined
  ];
}

function name(item) {
  let name = item && item.get && (item.get(n_name) || item.id) || item;
  if (name) name = name.toString();
  return name;
}

class EventBar extends Component {
  onconnected() {
    let point = this.attrs["point"];
    let start = this.attrs["start"];
    let end = this.attrs["end"];
    let color = this.attrs["color"];

    if (point) {
      start = point - 5;
      end = point + 5;
      this.classList.add("single");
    } else if (!end) {
      end = start + 10;
      this.classList.add("end");
    } else if (!start) {
      start = end - 10;
      this.classList.add("start");
    }

    this.style.width = (end - start) + "px";
    this.style.marginLeft = start + "px";
    if (color) this.style.background = color;
  }

  static stylesheet() {
    return `
      $ {
        display: block;
        position: relative;
        background: grey;
        height: 1em;
      }

      $.start {
        border-radius: 0.5em 0px 0px 0.5em;
      }

      $.end {
        border-radius: 0px  0.5em 0.5em 0px ;
      }

      $.single {
        border-radius: 0.5em 0.5em 0.5em 0.5em;
      }

      $:hover::before {
        content: attr(tooltip);
        font: 10pt arial;
        position: absolute;
        color: #F0F0F0;
        padding: 8px;
        border-radius: 5px;
        z-index: 1;
        margin-top: 25px;
        background: #606060;
        white-space: pre-wrap;
      }
    `;
  }
}

Component.register(EventBar);

function same_time(a, b) {
  if (!a && !b) return true;
  if (!a || !b) return false;
  return a.decimal() == b.decimal();
}

function add_event(events, topic, prop, value, begin, end) {
  for (let e of events) {
    if (e.topic == topic &&
        e.prop == prop &&
        e.value == value &&
        same_time(e.begin, begin) &&
        same_time(e.end, end)) {
      return;
    }
  }
  events.push({topic, prop, value, begin, end});
}

class TimelineDialog extends MdDialog {
  async init(topics) {
    // Get all topics for chart.
    this.sections = new Array();
    this.begin = undefined;
    this.end = undefined;
    let collector = new LabelCollector(store);
    for (let topic of topics) {
      let begin, end;

      // Get events for topic.
      let events = new Array();
      for (let [prop, value] of topic) {
        if (!qualified(value)) continue;
        if (!prop || !prop.get || prop.get(n_target) != n_item_type) continue;
        let [begin, end] = range(value);
        if (begin || end) {
          value = store.resolve(value);
          add_event(events, topic, prop, value, begin, end);
          collector.add_item(value);

          if (begin) {
            let b = begin.decimal();
            if (!this.begin || b < this.begin) this.begin = b;
          }
          if (end) {
            let e = end.next().decimal();
            if (!this.end || e > this.end) this.end = e;
          }
        }
      }

      for (let link of topic.links(true)) {
        let [b, e] = range(link);
        begin = begin || b;
        end = end || e;
        for (let [prop, value] of link) {
          if (!qualified(value)) continue;
          if (!prop || !prop.get || prop.get(n_target) != n_item_type) continue;
          let [begin, end] = range(value);
          if (begin || end) {
            value = store.resolve(value);
            add_event(events, topic, prop, value, begin, end);
            collector.add_item(value);

            if (begin) {
              let b = begin.decimal();
              if (!this.begin || b < this.begin) this.begin = b;
            }
            if (end) {
              let e = end.next().decimal();
              if (!this.end || e > this.end) this.end = e;
            }
          }
        }
      }

      await collector.retrieve();

      if (begin || end || events.length > 0) {
        events.sort((a, b) => {
          return (a.begin || a.end).decimal() - (b.begin || b.end).decimal();
        });
        this.sections.push({topic, begin, end, events});
      }
    }
    if (this.begin) this.begin = Math.floor(this.begin);
    if (this.end) this.end = Math.ceil(this.end);
  }

  onconnected() {
    this.bind("#close", "click", e => this.cancel());
  }

  render() {
    let widget = this.state;
    let width = Math.floor(window.innerWidth * 0.95 * 0.7);
    this.ppy = width / (this.end - this.begin);

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
      h += "<tr>";
      h += `<td class="sn">${name(s.topic)}</td>`;
      h += `<td class="sv" width=${width}">`;
      if (s.begin || s.end) {
        h += this.render_event_bar(s);
      }
      h += "</td>";
      h += "</tr>";
      for (let e of s.events) {
      h += "<tr>";
        h += `<td class="en">${name(e.prop)}: ${name(e.value)}</td>`;
        h += `<td class="ev" width=${width}>`;
        h += this.render_event_bar(e);
        h += "</td>";
        h += "</tr>";
      }
    }
    h += '</table>';
    h += '</div>';
    return h;
  }

  render_event_bar(e) {
    var point, start, end, color;
    if (e.begin == e.end) {
      console.log(e);
      point = Math.round((e.begin.decimal() - this.begin) * this.ppy);
    } else {
      if (e.begin) {
        start = Math.round((e.begin.decimal() - this.begin) * this.ppy);
      }
      if (e.end) {
        end = Math.round((e.end.next().decimal() - this.begin) * this.ppy);
      }
    }
    let title = name(e.topic);
    if (e.prop) {
      title += `\n${name(e.prop)}: ${name(e.value)}`;
      color = event_colors.get(e.prop);
    } else {
      color = "black";
    }
    if (e.begin || e.end) {
      title += "\n" + (e.begin ? e.begin.text() : "?") +
               " – " + (e.end ? e.end.text() : "?");
    }
    let tooltip = Component.escape(title).replaceAll(/ /g, "&nbsp;");

    let h = "<event-bar";
    if (point) h += ` point="${point}"`;
    if (start) h += ` start="${start}"`;
    if (end) h += ` end="${end}"`;
    if (color) h += ` color="${color}"`;
    if (tooltip) h +=` tooltip="${tooltip}"`;
    h += "></event-bar>";
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
        overflow: scroll;
        margin-bottom: 8px;
        white-space: nowrap;
      }

      $ .container table {
        table-layout: fixed;
        width: 100%;
      }

      $ .container tr:hover {
        background-color: #EEEEEE;
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


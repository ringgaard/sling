// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for importing tables from clipboard.

import {store} from "/case/app/global.js";
import {get_property_index} from "/case/app/schema.js";
import * as vp from "/case/app/value.js";
import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";

const n_id = store.id;
const n_is = store.is;
const n_target = store.lookup("target");

const n_time_type = store.lookup("/w/time");
const n_quantity_type = store.lookup("/w/quantity");
const n_geo_type = store.lookup("/w/geo");

var parsers = new Map();
parsers.set(n_time_type, vp.date_parser);
parsers.set(n_quantity_type, vp.quantity_parser);
parsers.set(n_geo_type, vp.geo_parser);

export default class TablePlugin {
  async process(action, data, context) {
    // Split data into lines.
    let lines = data.split("\n")
    if (lines.length < 2) return false;

    // Determine field separator.
    let header = lines[0];
    let sep = undefined;
    for (let c of ["\t", "|", ";", ","]) {
      if (header.includes(c)) {
        sep = c;
        break;
      }
    }
    if (!sep) return false;

    // Determine property for each field.
    let propidx = await get_property_index();
    let fields = header.split(sep);
    let columns = new Array();
    for (let name of fields) {
      let resolve = name.endsWith("#");
      if (resolve) name = name.slice(0, -1);
      let match = propidx.match(name, 1, false);
      if (match.length > 0) {
        let property = match[0];
        let dt = property.get(n_target);
        let parser = parsers.get(dt);
        columns.push({name, property, dt, resolve, parser});
      } else if (name.toLowerCase() == "id") {
        columns.push({name, property: n_is, resolve: false});
      } else {
        columns.push({name, property: name, dt: undefined});
      }
    }

    // Add remaining rows as new topics.
    for (let row = 1; row < lines.length; ++row) {
      let f = lines[row].split(sep);
      let cols = Math.min(f.length, columns.length);
      let topic = await context.new_topic();
      for (let col = 0; col < cols; ++col) {
        let column = columns[col];
        let name = column.property;
        let value = f[col];
        if (value.length == 0) continue;

        if (column.parser) {
          let results = new Array();
          column.parser(value, results);
          if (results.length > 0) value = results[0].value;
        }
        if (column.resolve) {
          value = store.lookup(value);
        }

        topic.put(name, value);
      }
    }

    return true;
  }
};


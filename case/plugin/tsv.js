// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING importer plug-in for importing data from TSV files.

import {store, frame} from "/common/lib/global.js";
import {parsers} from "/common/lib/datatype.js";

import {get_property_index} from "/case/app/schema.js";

const n_id = store.id;
const n_is = store.is;
const n_target = frame("target");

export default class TSVImporter {
  async process(file, context) {
    // Split data into lines.
    let data = await file.text();
    let lines = data.split("\n")
    if (lines.length < 2) return;

    // Determine property for each field.
    let header = lines[0];
    let propidx = await get_property_index();
    let fields = header.split("\t");
    let columns = new Array();
    for (let name of fields) {
      let resolve = name.endsWith("#");
      if (resolve) name = name.slice(0, -1);
      let match = await propidx.match(name, {limit: 1, full: true});
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
      let line = lines[row];
      if (line.length == 0) continue;
      let f = line.split("\t");
      let cols = Math.min(f.length, columns.length);
      if (cols == 0) continue;

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
          value = frame(value);
        }

        topic.put(name, value);
      }
    }
  }
};


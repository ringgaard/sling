// Copyright 2022 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING importer plug-in for importing data from RFC-4180 CSV files.

import {store, frame} from "/common/lib/global.js";
import {parsers} from "/common/lib/datatype.js";

import {get_property_index} from "/case/app/schema.js";

const n_id = store.id;
const n_is = store.is;
const n_target = frame("target");

class CSVParser {
  constructor(data) {
    this.data = data;
    this.pos = 0;
    this.size = data.length;
  }

  done() { return this.pos == this.size; }

  next() {
    let cols = new Array();
    let field = null;
    let inquote = false;
    while (this.pos < this.size) {
      let c = this.data.charAt(this.pos);
      if (c == '"') {
        if (inquote && this.data.charAt(this.pos + 1) == '"') {
          // Escaped quote.
          if (field === null) field = '';
          field += '"';
          this.pos += 2;
        } else {
          // Start or end of quoted field.
          this.pos++;
          inquote = !inquote;
        }
      } else if (c == ',') {
        if (inquote) {
          // Quoted delimiter.
          if (field === null) field = '';
          field += ',';
        } else {
          // End of field.
          cols.push(field);
          field = '';
        }
        this.pos++;
      } else if (c == '\r' || c == '\n') {
        if (inquote) {
          // Quoted newline.
          if (field === null) field = '';
          field += c;
          this.pos++;
        } else {
          // End of row.
          this.pos++;
          if (c == '\r' && this.data.charAt(this.pos) == '\n') this.pos++;
          break;
        }
      } else {
        if (field === null) field = '';
        field += c;
        this.pos++;
      }
    }
    if (field) cols.push(field);
    return cols;
  }
}

export default class CSVImporter {
  async process(file, context) {
    // Initialize CSV parser with file data.
    let data = await file.text();
    let parser = new CSVParser(data);

    // Determine property for each field.
    let propidx = await get_property_index();
    let fields = parser.next();
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
    while (!parser.done()) {
      let f = parser.next();
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


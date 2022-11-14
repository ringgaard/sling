// Copyright 2022 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING importer plug-in for importing data from JSON objects.

import {store, frame} from "/case/app/global.js";
import {get_property_index} from "/case/app/schema.js";
import {parsers} from "/case/app/value.js";

const n_id = store.id;
const n_is = store.is;
const n_target = frame("target");

export default class JSONImporter {
  async process(file, context) {
    // Parse JSON file.
    let data = (await file.text()).trim();
    var objects;
    if (data.charAt(0) == '[' && data.charAt(-1) == ']') {
      // Parse as an array of JSON objects.
      objects = JSON.parse(data);
    } else {
      // Parse each line as a JSON object.
      objects = new Array();
      for (let line of data.split('\n')) {
        objects.push(JSON.parse(line));
      }
    }

    function parse_value(prop, value) {
      var value;
      if (prop.parser && typeof(value) === 'string') {
        let results = new Array();
        prop.parser(value, results);
        if (results.length > 0) value = results[0].value;
      }
      if (prop.resolve) {
        value = frame(value);
      }
      return value;
    }

    // Add objects new topics.
    let propidx = await get_property_index();
    let properties = new Map();
    for (let obj of objects) {
      // Create new topic.
      let topic = await context.new_topic();

      // Add properties to topic.
      for (let [key, value] of Object.entries(obj)) {
        // Determine property for key.
        let prop = properties.get(key);
        if (!prop) {
          // New property.
          let name = key;
          let resolve = name.endsWith("#");
          if (resolve) name = name.slice(0, -1);
          let match = await propidx.match(name, {limit: 1, full: true});
          if (match.length > 0) {
            let property = match[0];
            let dt = property.get(n_target);
            let parser = parsers.get(dt);
            prop = {name, property, dt, resolve, parser};
          } else if (name.toLowerCase() == "id") {
            prop = {name, property: n_is, resolve: false};
          } else {
            prop = {name, property: name, dt: undefined};
          }
          properties.set(key, prop);
        }

        // Parse value.
        if (Array.isArray(value)) {
          for (let i = 0; i < value.length; i++) {
            let elem = parse_value(prop, value[i]);
            topic.put(prop.property, elem);
          }
        } else {
          value = parse_value(prop, value);
          topic.put(prop.property, value);
        }
      }
    }
  }
};


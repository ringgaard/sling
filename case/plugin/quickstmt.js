// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING importer plug-in for importing data from QuickStatements files.

import {store, frame} from "/common/lib/global.js";
import {Time} from "/common/lib/datatype.js";

const n_is = store.is;
const n_isa = store.isa;
const n_name = frame("name");
const n_alias = frame("alias");
const n_description = frame("description");
const n_target = frame("target");

const n_item_type = frame("/w/item");
const n_string_type = frame("/w/string");
const n_text_type = frame("/w/text");
const n_xref_type = frame("/w/xref");
const n_time_type = frame("/w/time");
const n_url_type = frame("/w/url");
const n_media_type = frame("/w/media");
const n_quantity_type = frame("/w/quantity");
const n_geo_type = frame("/w/geo");

const n_amount = frame("/w/amount");
const n_unit = frame("/w/unit");
const n_geo = frame("/w/geo");
const n_lat = frame("/w/lat");
const n_lng = frame("/w/lng");

function add(topic, name, value, lang) {
  if (value.charAt(0) == '"' && value.slice(-1) == '"') {
    value = value.slice(1, -1);
  }
  topic.add(name, store.localized(value, lang));
}

function convert(value, dt) {
  switch (dt) {
    case n_item_type: {
      return frame(value);
    }

    case n_text_type: {
      let lang = undefined;
      let m = value.match(/^(\w\w):(".+)$/);
      if (m) {
        lang = m[1];
        value = m[2];
      }
      if (value.charAt(0) == '"' && value.slice(-1) == '"') {
        value = value.slice(1, -1);
      }
      return store.localized(value, lang);
    }

    case n_xref_type:
    case n_url_type:
    case n_string_type:
    case n_media_type: {
      if (value.charAt(0) == '"' && value.slice(-1) == '"') {
        return value.slice(1, -1);
      } else {
        return value;
      }
    }

    case n_quantity_type: {
      let u = value.indexOf("U");
      if (u > 0) {
        let amount = parseFloat(value.slice(0, u));
        let unit = frame(value.slice(u + 1));
        let v = store.frame();
        v.add(n_amount, amount);
        v.add(n_unit, unit);
        return v;
      } else {
        return parseFloat(value)
      }
    }

    case n_time_type: {
      let t = Time.wikidate(value);
      return t && t.value();
    }

    case n_geo_type: {
      let m = value.match(/^@([0-9\.\+\-]+)\/([0-9\.\+\-]+)$/);
      if (!m) return null;
      let lat = parseFloat(m[1]);
      let lng = parseFloat(m[2]);
      let v = store.frame();
      v.add(n_isa, n_geo);
      v.add(n_lat, lat);
      v.add(n_lng, lng);
      return v;
    }
  }
}

export default class QuickStatementsImporter {
  async process(file, context) {
    // Split data into lines.
    let data = await file.text();
    let lines = data.split("\n")

    // Process each statements.
    let idmapping = new Map();
    let lineno = 1;
    for (let line of lines) {
      if (line.length == 0) continue;
      if (line == "CREATE") {
        // Create new empty topic.
        let topic = await context.new_topic();
        idmapping.set("LAST", topic);
      } else {
        // Split line into fields.
        let f = line.split("\t");
        if (f.length < 3) throw `Invalid QuickStatement, line ${lineno}`;
        let topicid = f[0];
        let propid = f[1];
        let propval = f[2];

        // Look up existing topic or create a new one.
        let topic = idmapping.get(topicid);
        if (!topic) {
          topic = await context.new_topic();
          topic.add(n_is, topicid);
          idmapping.set(topicid, topic);
        }
        idmapping.set("LAST", topic);

        // Add main value.
        let kind = propid.charAt(0);
        if (kind == "L") {
          add(topic, n_name, propval, propid.slice(0));
        } else if (kind == "A") {
          add(topic, n_alias, propval, propid.slice(0));
        } else if (kind == "D") {
          add(topic, n_description, propval, propid.slice(0));
        } else {
          let property = frame(propid);
          let dt = property.get(n_target);
          let value = convert(propval, dt);
          if (f.length > 3) {
            let qualifiers = store.frame();
            qualifiers.add(n_is, value);
            for (let i = 3; i < f.length; i += 2) {
              let qproperty = frame(f[i]);
              let dt = qproperty.get(n_target);
              let qvalue = convert(f[i + 1], dt);
              if (qvalue) qualifiers.add(qproperty, qvalue);
            }
            value = qualifiers;
          }
          if (value) topic.add(property, value);
        }
      }
      lineno++;
    }
  }
}


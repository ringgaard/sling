// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Knowledge base schema.

import {Component} from "/common/lib/component.js";
import {MdSearchResult} from "/common/lib/material.js";
import {Frame} from "/common/lib/frame.js";
import {store, settings} from "./global.js";

const n_is = store.is;
const n_id = store.id;
const n_name = store.lookup("name");
const n_alias = store.lookup("alias");
const n_description = store.lookup("description");
const n_instance_of = store.lookup("P31");
const n_inverse_property = store.lookup("P1696");

const n_properties = store.lookup("properties");
const n_fanin = store.lookup("/w/item/fanin");

const max_fainin = 1000000;

n_is.add(n_name, "is");
n_is.add(n_description, "same as");
n_is.add(n_fanin, max_fainin);

n_name.add(n_name, "name");
n_name.add(n_description, "item name");
n_name.add(n_fanin, max_fainin);

n_description.add(n_name, "description");
n_description.add(n_description, "item description");
n_description.add(n_fanin, max_fainin);

n_instance_of.add(n_name, "instance of");
n_instance_of.add(n_description, "item type");

n_alias.add(n_name, "alias");
n_alias.add(n_description, "item alias");
n_alias.add(n_fanin, max_fainin);

const property_shortcuts = {
  "is": n_is,
  "name": n_name,
  "description": n_description,
  "alias": n_alias,
  "type": n_instance_of,
};

var kbschema;
var kbpropidx;

function normalized(str) {
  return str.toString().toLowerCase();
}

class PropertyIndex {
  constructor(schema) {
    // Collect all property names and aliases.
    this.ids = new Map();
    this.names = new Array();
    for (let property of schema.get(n_properties)) {
      this.ids.set(property.id, property);
      for (let name of property.all(n_name)) {
        this.names.push({name: normalized(name), property});
      }
      for (let alias of property.all(n_alias)) {
        this.names.push({name: normalized(alias), property, alias: true});
      }
    }

    // Sort property names.
    this.names.sort((a, b) => {
      if (a.name < b.name) return -1;
      if (a.name > b.name) return 1;
      return 0;
    });
  }

  match(query, options = {}) {
    // Normalize query.
    let normalized_query = normalized(query);
    let prefix = !options.full;
    let limit = options.limit || 30;

    // Add property shortcut matches.
    let matches = new Set();
    let scores = new Map();
    let query_len = normalized_query.length;
    for (let [name, value] of Object.entries(property_shortcuts)) {
      if (name.substring(0, query_len) == normalized_query) {
        matches.add(value);
        scores.set(value, 1000);
      }
    }

    // Add matching property id.
    let prop = this.ids.get(query);
    if (prop) matches.add(prop);

    // Find first name that is greater than or equal to the prefix.
    let lo = 0;
    let hi = this.names.length - 1;
    while (lo < hi) {
      let mid = (lo + hi) >> 1;
      let entry = this.names[mid];
      if (entry.name < normalized_query) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }

    // Find all names matching the prefix. Stop if we hit the limit.
    let index = lo;
    while (index < this.names.length) {
      // Check if we have reached the limit.
      if (matches.length > limit) break;

      // Stop if the current name does not match (the prefix).
      let entry = this.names[index];
      let fullmatch = entry.name == normalized_query
      if (prefix) {
        if (!entry.name.startsWith(normalized_query)) break;
      } else {
        if (!fullmatch) break;
      }

      let prop = entry.property;
      matches.add(prop);
      let score = scores.get(prop) || 1;
      if (!entry.alias) score = 100;
      if (fullmatch) score *= 100;
      scores.set(prop, score);
      index++;
    }

    // Rank search results.
    let results = Array.from(matches);
    results.sort((a, b) => {
      let fa = (a.get(n_fanin) || 1) * scores.get(a);
      let fb = (b.get(n_fanin) || 1) * scores.get(b);
      return fb - fa;
    });

    return results;
  }
};

export async function get_schema() {
  // Return schema if it has already been fetched.
  if (kbschema) return kbschema;

  // Fetch schema from server and decode it.
  let response = await fetch(settings.kbservice + "/schema/");
  if (kbschema) return kbschema;
  kbschema = await store.parse(response);

  return kbschema;
}

export async function get_property_index() {
  // Return property index if it has already been built.
  if (kbpropidx) return kbpropidx;

  // Get schema.
  let schema = await get_schema();
  if (kbpropidx) return kbpropidx;

  // Build property index.
  kbpropidx = new PropertyIndex(schema);
  return kbpropidx;
}

export function qualified(v) {
  if (v instanceof Frame) {
    return v.has(n_is) && !v.has(n_id);
  } else {
    return false;
  }
}

export function inverse_property(property, source) {
  if (source) {
    if (!(property instanceof Frame)) return;
    let inverse = property.get(n_inverse_property);
    if (!inverse) return;
    if (qualified(inverse)) {
      // Find inverse property which matches the target, e.g. gendered
      // properties.
      var fallback;
      for (let p of property.all(n_inverse_property)) {
        if (qualified(p)) {
          let match = true;
          inverse = null;
          for (let [n, v] of p) {
            if (n == n_is) {
              inverse = v;
            } else if (!source.has(n, v)) {
              match = false;
            }
          }
          if (match && inverse) return inverse;
        } else {
          fallback = p;
        }
      }
      return fallback;
    } else {
      return inverse;
    }
  } else {
    let props = new Array();
    for (let p of property.all(n_inverse_property)) {
      if (qualified(p)) p = p.get(n_is);
      props.push(p);
    }
    return props;
  }
}

export async function psearch(query, results, options) {
  let props = await get_property_index();
  let matches = props.match(query, options);
  for (let match of matches) {
    results.push({
      ref: match.id,
      name: match.get(n_name),
      description: match.get(n_description),
      property: match,
    });
  }
}


// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Knowledge base schema.

import {Component} from "/common/lib/component.js";
import {MdSearchResult} from "/common/lib/material.js";
import {store, settings} from "./global.js";

const n_name = store.lookup("name");
const n_alias = store.lookup("alias");
const n_description = store.lookup("description");
const n_properties = store.lookup("properties");
const n_fanin = store.lookup("/w/item/fanin");

var kbschema;
var kbpropidx;

function normalized(str) {
  return str.toString().toLowerCase();
}

class PropertyIndex {
  constructor(schema) {
    // Collect all property names and aliases.
    this.names = new Array();
    for (let property of schema.get(n_properties)) {
      for (let name of property.all(n_name)) {
        this.names.push({name: normalized(name), property});
      }
      for (let alias of property.all(n_alias)) {
        this.names.push({name: normalized(alias), property});
      }
    }

    // Sort property names.
    this.names.sort((a, b) => {
      if (a.name < b.name) return -1;
      if (a.name > b.name) return 1;
      return 0;
    });
  }

  match(query, limit=30, prefix=true) {
    // Normalize query.
    let normalized_query = normalized(query);

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
    let matches = new Set();
    let index = lo;
    while (index < this.names.length) {
      // Check if we have reached the limit.
      if (matches.length > limit) break;

      // Stop if the current name does not match (the prefix).
      let entry = this.names[index];
      if (prefix) {
        if (!entry.name.startsWith(normalized_query)) break;
      } else {
        if (entry.name != normalized_query) break;
      }

      matches.add(entry.property);
      index++;
    }

    // Rank search results.
    let results = Array.from(matches);
    results.sort((a, b) => {
      let fa = a.get(n_fanin) || 0;
      let fb = b.get(n_fanin) || 0;
      return fb - fa;
    });

    return results;
  }
};

export async function get_schema() {
  // Return schema if it has already been fetched.
  if (kbschema) return kbschema;

  // Fetch schema from server and decode it.
  console.log("fetch schema");
  let start = performance.now();
  let response = await fetch(settings.kbservice + "/schema/");
  if (kbschema) return kbschema;
  kbschema = await store.parse(response);
  let end = performance.now();
  console.log("schema done", (end - start), store.frames.size);

  // Mark all properties as stubs.
  for (let type of kbschema.get("properties")) {
    type.markstub();
  }

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

class PropertySearchBox extends Component {
  onconnected() {
    this.bind("md-search", "query", e => this.onquery(e));
  }

  async onquery(e) {
    let target = e.target;
    let detail = e.detail
    let query = detail.trim();
    let props = await get_property_index();

    let matches = props.match(query);
    let items = [];
    for (let match of matches) {
      items.push(new MdSearchResult({
        ref: match.id,
        name: match.get(n_name),
        description: match.get(n_description),
        property: match,
      }));
    }
    target.populate(detail, items);
  }

  render() {
    return `
      <form>
        <md-search
          placeholder="Search for property..."
          min-length=2
          autoselect=1>
        </md-search>
      </form>
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: block;
        width: 100%;
        max-width: 800px;
        padding-left: 10px;
        padding-right: 3px;
      }

      $ form {
        display: flex;
        width: 100%;
      }
    `;
  }
}

Component.register(PropertySearchBox);


// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Knowledge base schema.

import {Frame} from "/common/lib/frame.js";
import {store, frame, settings} from "/common/lib/global.js";

import {SearchIndex} from "./search.js";

const n_is = store.is;
const n_id = store.id;
const n_name = frame("name");
const n_alias = frame("alias");
const n_description = frame("description");
const n_instance_of = frame("P31");
const n_inverse_property = frame("P1696");
const n_properties_for_type = frame("P1963");
const n_property_constraint = frame("P2302");
const n_allowed_qualifiers_constraint = frame("Q21510851");
const n_property = frame("P2306");
const n_formatter_url = frame("P1630");
const n_matcher = frame("P8460");

const n_properties = frame("properties");
const n_rank = frame("rank");
const n_fanin = frame("/w/item/fanin");

const max_fanin = Number.POSITIVE_INFINITY;

n_is.add(n_name, "is");
n_is.add(n_description, "same as");
n_is.add(n_fanin, max_fanin);

n_name.add(n_name, "name");
n_name.add(n_description, "item name");
n_name.add(n_fanin, max_fanin);

n_description.add(n_name, "description");
n_description.add(n_description, "item description");
n_description.add(n_fanin, max_fanin);

n_instance_of.add(n_name, "instance of");
n_instance_of.add(n_description, "item type");
n_instance_of.add(n_fanin, max_fanin);

n_alias.add(n_name, "alias");
n_alias.add(n_description, "item alias");
n_alias.add(n_fanin, max_fanin);

const property_shortcuts = {
  "is": n_is,
  "name": n_name,
  "description": n_description,
  "alias": n_alias,
};

var kbschema;
var kbprops;

class Properties {
  constructor(schema) {
    // Build property search index.
    this.index = new SearchIndex(schema.get(n_properties));

    // Type cache with properties for types.
    this.types = new Map();
  }

  async properties_for(type) {
    // Check if type properties are already cached.
    if (!type) return;
    if (!(type instanceof Frame)) return;
    let props = type.get(type);
    if (props) return props;

    // Retrieve type if needed.
    if (!type.ispublic()) {
      let url = `${settings.kbservice}/kb/topic?id=${type.id}`;
      let response = await fetch(url);
      if (!response.ok) return;
      type = await store.parse(response);
    }

    // Get properties for type.
    let properties = new Set();
    for (let prop of type.all(n_properties_for_type)) {
      properties.add(store.resolve(prop));
    }

    // Update cache.
    this.types.set(type, properties);

    return properties;
  }

  qualifiers_for(prop) {
    if (!prop) return;
    if (!(prop instanceof Frame)) return;

    for (let c of prop.all(n_property_constraint)) {
      if (store.resolve(c) == n_allowed_qualifiers_constraint) {
        let properties = new Set();
        for (let p of c.all(n_property)) {
          properties.add(store.resolve(p))
        }
        return properties;
      }
    }
  }

  async match(query, options = {}) {
    // Add property shortcut matches.
    let matches = new Map();
    let query_len = query.length;
    for (let [name, value] of Object.entries(property_shortcuts)) {
      if (name.substring(0, query_len) == query) {
        matches.set(value, value.get(n_fanin));
      }
    }

    // Get properties for type, occupation, and qualifier ranking.
    let type_props = await this.properties_for(options.type);
    let occ_props = await this.properties_for(options.occupation);
    let qual_props = this.qualifiers_for(options.qualify);

    // Search for matching propeties.
    for (let hit of this.index.hits(query, options)) {
      // Compute ranking score for match.
      let prop = hit.topic;
      let score = matches.get(prop) || prop.get(n_fanin) || 1;
      if (!hit.alias) score *= 30;
      if (hit.full) score *= 10;
      if (type_props && type_props.has(prop)) score *= 100;
      if (occ_props && occ_props.has(prop)) score *= 10;
      if (qual_props && qual_props.has(prop)) score *= 1000;
      matches.set(prop, score);
    }

    // Rank search results.
    let results = Array.from(matches.keys());
    results.sort((a, b) => {
      return matches.get(b) - matches.get(a);
    });

    return results.slice(0, options.limit);
  }
}

class URLFormatter {
  constructor(prop) {
    let rank = 1;
    for (let f of prop.all(n_formatter_url)) {
      if (f instanceof Frame) {
        let r = f.get(n_rank);
        if (r === undefined) r = 1;
        let regex = f.get(n_matcher);
        if (regex) {
          if (!this.variants) this.variants = new Array();
          this.variants.push({
            pattern: new RegExp("^" + regex + "$"),
            formatter: f.get(n_is),
          });
        } else if (!this.formatter || r > rank) {
          this.formatter = f.get(n_is);
          rank = r;
        }
      } else if (!this.formatter || rank < 1) {
        this.formatter = f;
        rank = 1;
      }
    }
  }

  url(value) {
    if (this.variants) {
      for (let v of this.variants) {
        if (v.pattern.test(value)) {
          return v.formatter.replace("$1", value);
        }
      }
    }
    if (this.formatter) {
      return this.formatter.replace("$1", value);
    }
  }
}

var formatters = new Map();

export function url_format(prop, value) {
  let formatter = formatters.get(prop);
  if (!formatter) {
    formatter = new URLFormatter(prop);
    formatters.set(prop, formatter);
  }
  return formatter.url(value);
}

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
  if (kbprops) return kbprops;

  // Get schema.
  let schema = await get_schema();
  if (kbprops) return kbprops;

  // Build property index.
  kbprops = new Properties(schema);
  return kbprops;
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
  let matches = await props.match(query, options);
  for (let match of matches) {
    results.push({
      ref: match.id,
      name: match.get(n_name),
      description: match.get(n_description),
      property: match,
    });
  }
}


// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {MdSearchResult} from "/common/lib/material.js";

import {store, settings} from "./global.js";

const n_name = store.lookup("name");
const n_alias = store.lookup("alias");

function normalized(str) {
  return str.toString().toLowerCase();
}

export async function search(query, backends, options = {}) {
  query = query.trim();
  if (query.endsWith(".")) {
    // Do full matching if query ends with period.
    options.full = true;
    query = query.slice(0, -1);
  } else if (query.endsWith("?")) {
    // Do keyword matching if query ends with question mark.
    options.keyword = true;
    query = query.slice(0, -1);
  }

  let results = new Array();
  if (query.length > 0) {
    // Collect search results from backends.
    let items = new Array();
    for (let backend of backends) {
      await backend(query, items, options);
    }

    // Convert items to search results filtering out duplicates.
    let seen = new Set();
    for (let item of items) {
      if (item.ref) {
        if (seen.has(item.ref)) continue;
        seen.add(item.ref);
      }
      if (item.topic) {
        for (let ref of item.topic.links()) seen.add(ref.id);
      }
      results.push(new MdSearchResult(item));
    }
  }

  return results;
}

export async function kbsearch(query, results, options) {
  try {
    let path = options.keyword ? "/kb/search" : "/kb/query";
    let params = "fmt=cjson";
    if (options.full) params += "&fullmatch=1";
    if (options.property) params += "&prop=" + options.property.id;
    params += `&q=${encodeURIComponent(query)}`;

    let response = await fetch(`${settings.kbservice}${path}?${params}`);
    let data = await response.json();
    for (let item of data.matches) {
      results.push({
        ref: item.ref,
        name: item.text,
        description: item.description,
      });
    }
  } catch (error) {
    console.log("Query error", query, error.message, error.stack);
  }
}

export class SearchIndex {
  constructor(topics) {
    this.topics = topics;
    this.ids = new Map();
    this.names = new Array();
    for (let topic of topics) {
      // Add topic ids to id index.
      this.ids.set(topic.id, topic);
      for (let link of topic.links()) {
        this.ids.set(link.id, topic)
      }

      // Add topic names and aliases to index.
      for (let name of topic.all(n_name)) {
        this.names.push({name: normalized(name), topic});
      }
      for (let alias of topic.all(n_alias)) {
        this.names.push({name: normalized(alias), topic, alias: true});
      }
    }

    // Sort topic names.
    this.names.sort((a, b) => {
      if (a.name < b.name) return -1;
      if (a.name > b.name) return 1;
      return 0;
    });
  }

  hits(query, options = {}) {
    let it = function* (names, ids, topics) {
      // Normalize query.
      let normalized_query = normalized(query);

      // Add matching id.
      let topic = ids.get(query);
      if (topic) yield {topic, full: true};

      // Find first name that is greater than or equal to the query.
      let lo = 0;
      let hi = names.length - 1;
      while (lo < hi) {
        let mid = (lo + hi) >> 1;
        let entry = names[mid];
        if (entry.name < normalized_query) {
          lo = mid + 1;
        } else {
          hi = mid;
        }
      }

      // Find all names matching the query. Stop if we hit the limit.
      for (let index = lo; index < names.length; ++index) {
        // Stop if the current name does not match (the prefix).
        let entry = names[index];
        let full = entry.name == normalized_query;
        if (options.full) {
          if (!full) break;
        } else {
          if (!entry.name.startsWith(normalized_query)) break;
        }

        yield {topic: entry.topic, alias: entry.alias, full};
      }

      // Add submatches.
      if (options.keyword) {
        for (let topic of topics) {
          for (let name of topic.all(n_name)) {
            if (normalized(name).includes(normalized_query)) {
              yield {topic, sub: true};
            }
          }
          for (let name of topic.all(n_alias)) {
            if (normalized(name).includes(normalized_query)) {
              yield {topic, sub: true, alias: true};
            }
          }
        }
      }
    }

    return it(this.names, this.ids, this.topics);
  }
}

export class OmniBox extends Component {
  constructor(state) {
    super(state);
    this.backends = new Array();
  }

  add(backend) {
    this.backends.push(backend);
  }

  onconnected() {
    this.bind("md-search", "query", e => this.onquery(e));
  }

  async onquery(e) {
    let query = e.detail
    let target = e.target;
    let results = await search(query, this.backends);
    target.populate(query, results);
  }

  async set(query) {
    let searchbox = this.find("md-search");
    searchbox.set(query);
    let results = await search(query, this.backends);
    searchbox.populate(query, results);
  }

  query() {
    return this.find("md-search").query();
  }

  clear() {
    return this.find("md-search").clear();
  }

  render() {
    return `
      <md-search
        placeholder="Search for topic, press Enter for new topic"
        min-length=2>
      </md-search>
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
        width: 100%;
      }

      $ {
        display: block;
        width: 100%;
        max-width: 800px;
        padding-left: 10px;
        padding-right: 10px;
      }
    `;
  }
}

Component.register(OmniBox);


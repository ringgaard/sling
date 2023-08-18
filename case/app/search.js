// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {MdSearchResult, MdDialog} from "/common/lib/material.js";
import {store, frame, settings} from "/common/lib/global.js";

const n_name = frame("name");
const n_alias = frame("alias");
const n_birth_name = frame("P1477");
const n_target = frame("target");
const n_xref_type = frame("/w/xref");

export function normalized(str) {
  return str
    .toString()
    .normalize("NFD")
    .replace(/[\u0300-\u036f]/g, "")
    .toLowerCase();
}

export async function search(queries, backends, options = {}) {
  let items = new Array();
  if (Array.isArray(queries)) {
    for (let query of queries) {
      if (query.length == 0) continue;
      for (let backend of backends) {
        await backend(query, items, options);
      }
    }
  } else {
    let query = queries.trim();
    if (query.endsWith(".")) {
      // Do full matching if query ends with period.
      options.full = true;
      query = query.slice(0, -1);
    } else if (query.endsWith("?")) {
      // Do keyword matching if query ends with question mark.
      options.keyword = true;
      query = query.slice(0, -1);
    }

    // Collect search results from backends.
    if (query.length > 0) {
      for (let backend of backends) {
        await backend(query, items, options);
      }
    }
  }

  // Convert items to search results filtering out duplicates.
  let results = new Array();
  let seen = new Set();
  let ignore = options.ignore;

  for (let item of items) {
    let ref = item.ref;
    if (options.local) {
      let topic = options.local.ids.get(ref)
      if (topic) ref = topic.id;
    }

    if (ref) {
      if (ignore && ignore.includes(ref)) continue;
      if (seen.has(ref)) continue;
      seen.add(ref);
    }
    if (item.topic) {
      for (let r of item.topic.links()) seen.add(r.id);
    }
    results.push(new MdSearchResult(item));
  }

  return results;
}

export async function kbsearch(query, results, options) {
  if (query.length > 1024) return;
  try {
    let path = options.keyword ? "/kb/search" : "/kb/query";
    let params = "fmt=cjson";
    if (options.full) params += "&fullmatch=1";
    if (options.property) params += "&prop=" + options.property.id;
    params += `&q=${encodeURIComponent(query)}`;

    let response = await fetch(`${settings.kbservice}${path}?${params}`);
    if (!response.ok) throw `Search failed: ${response.statusText}`;
    let data = await response.json();
    let twiddle = true;
    for (let item of data.matches) {
      let result = {
        ref: item.ref,
        name: item.text,
        description: item.description,
      };
      if (twiddle && item.score > 1000000) {
        if (!options.local || !options.local.ids.get(item.ref)) {
          results.unshift(result);
        }
        twiddle = false;
      } else {
        results.push(result);
      }
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

      // Add topic names, aliases, and xrefs to index.
      for (let [prop, value] of topic) {
        if (prop == n_name || prop == n_birth_name || prop == n_alias) {
          let alias = prop == n_alias;
          this.names.push({name: normalized(value), topic, alias});
        } else if (prop && prop.get && prop.get(n_target) == n_xref_type) {
          let xref = store.resolve(value);
          let identifier = `${prop.id}/${xref}`;
          if (!this.ids.has(identifier)) this.ids.set(identifier, topic);
        }
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
      if (options.keyword || options.submatch) {
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

    let options = {};
    let editor = this.match("#editor");
    if (editor) options.local = editor.get_index();

    let results = await search(query, this.backends, options);
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

export class SearchResultsDialog extends MdDialog {
  async onconnected() {
    this.attach(this.onkeydown, "keydown");
    this.attach(this.onselect, "select", "#items");
    this.find("#items").update({items: this.state.items});
  }

  onkeydown(e) {
    let list = this.find("#items");
    if (e.keyCode == 40) {
      list.next();
    } else if (e.keyCode == 38) {
      list.prev();
    }
  }

  async onselect(e) {
    let ref = e.detail.item.state.ref;
    this.close(ref);
  }

  submit() {
    let list = this.find("#items");
    if (list.active) {
      let ref = list.active.state.ref;
      this.close(ref);
    }
  }

  render() {
    return `
      <md-dialog-top>${this.state.title}</md-dialog-top>
      <md-search-list id="items"></md-search-list>
      <md-dialog-bottom>
        <button id="cancel">Cancel</button>
      </md-dialog-bottom>
    `;
  }

  static stylesheet() {
    return `
      $ {
        width: 500px;
        max-height: 80vh;
      }
      $ md-search-list {
        position: relative;
      }
      $ md-dialog-bottom {
        padding-top: 16px;
      }
    `;
  }
}

Component.register(SearchResultsDialog);


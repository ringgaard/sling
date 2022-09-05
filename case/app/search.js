// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {MdSearchResult} from "/common/lib/material.js";

import {store, settings} from "./global.js";

export async function search(query, backends, options) {
  // Do full match if query ends with period.
  if (query.endsWith(".")) {
    options.full = true;
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
    let detail = e.detail
    let target = e.target;
    let query = detail.trim();
    let results = await search(query, this.backends);
    target.populate(detail, results);
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


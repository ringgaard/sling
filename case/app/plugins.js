// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-ins.

import {settings} from "./global.js";

// Actions.
export const SEARCH    = 0;
export const SEARCHURL = 1;
export const PASTE     = 2;
export const PASTEURL  = 3;

// Case plug-ins.
var plugins = [

// Wikidata item.
{
  name: "wikidata",
  module: "wikidata.js",
  actions: [SEARCHURL],
  patterns: [
    /^https:\/\/(www\.)?wikidata\.org\/wiki\//,
  ],
},

// Twitter profiles.
{
  name: "twitter",
  module: "twitter.js",
  actions: [PASTEURL, SEARCHURL],
  patterns: [
    /^https:\/\/(mobile\.)?twitter\.com\//,
  ],
},

// Linktree profiles.
{
  name: "linktree",
  module: "linktree.js",
  actions: [PASTEURL, SEARCHURL],
  patterns: [
    /^https:\/\/linktr.ee\//,
  ],
},

// Babepedia profiles.
{
  name: "babepedia",
  module: "babepedia.js",
  actions: [PASTEURL, SEARCHURL],
  patterns: [
    /^https:\/\/www\.babepedia\.com\/babe\//,
  ],
},

// Se & Hør profiles from hjemmestrik.dk.
{
  name: "hjemmestrik",
  module: "hjemmestrik.js",
  actions: [PASTEURL, SEARCHURL],
  patterns: [
    /^https:\/\/hjemmestrik.dk\/pige\//,
  ],
},

// Images from forum posts.
{
  name: "forum",
  module: "forum.js",
  actions: [PASTEURL],
  patterns: [
    /^https?:\/\/[A-Za-z0-9\-\_\.]+\/showpost.php\?/,
  ],
},

// Photo albums from Reddit and Imgur.
{
  name: "albums",
  module: "albums.js",
  actions: [PASTEURL],
  patterns: [
    /^https?:\/\/(i\.|www\|m\.)?imgur.com\//,
    /^https?:\/\/(www\.|old\.)?reddit\.com\//,
    /^https?:\/\/i\.redd\.it\//,
    /^https?:\/\/i\.redditmedia\.com\//,
    /^https?:\/\/preview\.redd\.it\//,
    /^https?:\/\/asset\.dr\.dk\//,
  ],
},

// JPG, GIF and PNG images.
{
  name: "images",
  module: "images.js",
  actions: [PASTEURL],
  patterns: [
    /^https?:\/\/.*\.(jpg|jpeg|gif|png)([\/\?].+)?$/,
  ],
},

];

function parse_url(url) {
  try {
    return new URL(url);
  } catch (_) {
    return null;
  }
}

export class Context {
  constructor(topic, casefile, editor) {
    this.topic = topic;
    this.casefile = casefile;
    this.editor = editor;
  }

  new_topic() {
    return editor.new_topic();
  }

  service(name, params) {
    let url = `/case/service/${name}`;
    if (params) {
      let qs = new URLSearchParams(params);
      url += "?" + qs.toString();
    }
    return url;
  }

  proxy(url) {
    return `/case/proxy?url=${encodeURIComponent(url)}`;
  }

  kblookup(query, params) {
    let qs = new URLSearchParams(params);
    qs.append("q", query);
    qs.append("fmt", "cjson");
    let url = `${settings.kbservice}/kb/query?${qs}`;
    return fetch(url);
  }
};

export async function process(action, query, context) {
  // Check for URL query.
  let url = parse_url(query);
  if (url) {
    if (action == SEARCH) action = SEARCHURL;
    if (action == PASTE) action = PASTEURL;
  } else if (action == SEARCHURL || action == PASTEURL) {
    return false;
  }

  // Try to find plug-in with a matching pattern.
  let plugin = null;
  for (let p of plugins) {
    if (p.actions.includes(action)) {
      for (let pattern of p.patterns) {
        if (query.match(pattern)) {
          plugin = p;
          break;
        }
      }
    }
    if (plugin) break;
  }
  if (!plugin) return false;

  // Load module if not already done.
  if (!plugin.instance) {
    let module_url = `/case/plugin/${plugin.module}`;
    console.log(`Load plugin ${plugin.name} from ${module_url}`);
    const { default: component } = await import(module_url);
    plugin.instance = new component();
  }

  // Let plugin process the query.
  console.log(`Run plugin ${plugin.name} for '${query}'`);
  return plugin.instance.process(action, query, context);
}


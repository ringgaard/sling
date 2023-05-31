// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-ins.

import {store, frame, settings} from "/common/lib/global.js";

import {xref_patterns} from "./social.js";

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

// Wikipedia page.
{
  name: "wikipedia",
  module: "wikipedia.js",
  actions: [PASTEURL, SEARCHURL],
  patterns: [
    /^https:\/\/\w+\.wikipedia\.org\/wiki\//,
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

// IMDB biographies.
{
  name: "imdb",
  module: "imdb.js",
  actions: [PASTEURL, SEARCHURL],
  patterns: [
    /^https:\/\/www.imdb.com\/name\/(nm\d+)\/bio/,
  ],
},

// VIAF entity.
{
  name: "viaf",
  module: "viaf.js",
  actions: [PASTEURL, SEARCHURL],
  patterns: [
    /^https?:\/\/viaf\.org\/viaf\/\d+/,
  ],
},

// CVR entities.
{
  name: "cvr",
  module: "cvr.js",
  actions: [PASTEURL, SEARCHURL],
  patterns: [
    /^https:\/\/datacvr\.virk\.dk\/data\/visenhed/,
  ],
},

// OpenCorporates companies.
{
  name: "opencorp",
  module: "opencorp.js",
  actions: [PASTEURL, SEARCHURL],
  patterns: [
    /^https:\/\/opencorporates\.com\/companies\//,
  ],
},

// E-mail address.
{
  name: "email",
  module: "email.js",
  actions: [PASTE],
  patterns: [
    /^[A-Za-z0-9_.+-]+@[A-Za-z0-9-]+\.[A-Za-z0-9-.]+$/,
  ],
},

// Phone number.
{
  name: "phone",
  module: "phone.js",
  actions: [PASTE],
  patterns: [
    /^\+\d+[0-9- ]*$/,
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

// Google Maps.
{
  name: "gmaps",
  module: "gmaps.js",
  actions: [PASTEURL, SEARCHURL],
  patterns: [
    /^https:\/\/www\.google\.com\/maps\/place\//,
  ],
},

// Listal profiles.
{
  name: "listal",
  module: "listal.js",
  actions: [PASTEURL, SEARCHURL],
  patterns: [
    /https:\/\/www.listal.com\//,
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

// IAFD profiles.
{
  name: "iafd",
  module: "iafd.js",
  actions: [PASTEURL, SEARCHURL],
  patterns: [
    /^https?:\/\/www\.iafd\.com\/person\.rme\/perfid=(\w+)\/?/,
  ],
},

// Actresses from glamourgirlsofthesilverscreen.com.
{
  name: "ggss",
  module: "ggss.js",
  actions: [PASTEURL],
  patterns: [
    /^http:\/\/www\.glamourgirlsofthesilverscreen\.com\//,
  ],
},

// Famous persons from famousbirthdays.com.
{
  name: "fambdays",
  module: "fambdays.js",
  actions: [PASTEURL, SEARCHURL],
  patterns: [
    /^https?\:\/\/www\.famousbirthdays\.com\/people\//,
  ],
},

// E-books from libgen.
{
  name: "libgen",
  module: "libgen.js",
  actions: [PASTEURL, SEARCHURL],
  patterns: [
    /^http\:\/\/library\.lol\//,
    /^https:\/\/libgen.is\/book\/index.php\?md5=/,
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

// Social media profiles from r/BeautifulFemales etc.
{
  name: "beautyfem",
  module: "beautyfem.js",
  actions: [PASTEURL, SEARCHURL],
  patterns: [
    /^https:\/\/www\.reddit\.com\/r\/BeautifulFemales\/comments\//,
    /^https:\/\/www\.reddit\.com\/r\/(HoneyDrip|HoneyDripSFW)\/comments\//,
  ],
},

// Photo albums from Reddit and Imgur.
{
  name: "albums",
  module: "albums.js",
  actions: [PASTEURL],
  patterns: [
    /^https?:\/\/(i\.|www\|m\.)?imgur.com\//,
    /^https?:\/\/(www\.|old\.)?reddit\.com\/gallery\//,
    /^https?:\/\/(www\.|old\.)?reddit\.com\/\w+\/\w+\/comments\//,
    /^https?:\/\/i\.redd\.it\//,
    /^https?:\/\/i\.redditmedia\.com\//,
    /^https?:\/\/preview\.redd\.it\//,
    /^https?:\/\/asset\.dr\.dk\//,
    /^https?:\/\/imgchest\.com\/p\//,
    /^https?:\/\/www\.imgchest\.com\/p\//,
  ],
},

// Video transcoding.
{
  name: "transcode",
  module: "transcode.js",
  actions: [PASTEURL],
  patterns: [
    /^https?:\/\/.+\.(avi|wmv|mp4)(\/file)?$/,
  ],
},

// Images from forum posts.
{
  name: "forum",
  module: "forum.js",
  actions: [PASTEURL],
  patterns: [
    /^https?:\/\/[A-Za-z0-9\-\_\.]+\/showpost.php\?/,
    /^https?:\/\/[A-Za-z0-9\-\_\.]+\/(galleries|gallery|albums|girls|gals)\//,
    /^https?:\/\/[A-Za-z0-9\-\_\.]+\/[a-z\-]+\-(gallery|galley)$/,
    /^https?:\/\/www\.in\-the\-raw\.org\//,
    /^https?:\/\/forum\.burek\.com\//,
    /^https?:\/\/glam0ur\.net\//,
    /^https?:\/\/celeb\.gate\.cc\//,
    /^https?:\/\/forum\.phun\.org\/threads\//,
    /^https?:\/\/.+\/gallery.html$/,
    /^https?:\/\/thekameraclub\.co\.uk\//,
    /^https?:\/\/p..ncoven\.com\//,
  ],
},

// Knowledge base links.
{
  name: "kb",
  module: "kb.js",
  actions: [PASTEURL],
  patterns: [
    /^https?:\/\/ringgaard\.com\/kb\//,
  ],
},

// Photos from instagram.
{
  name: "instagram",
  module: "instagram.js",
  actions: [PASTEURL],
  patterns: [
    /^https:\/\/www\.instagram\.com\/p\//i,
  ],
},

// Cross-reference links.
{
  name: "xref",
  module: "xrefs.js",
  actions: [PASTEURL, SEARCHURL],
  patterns: xref_patterns(),
},

// Media files.
{
  name: "images",
  module: "images.js",
  actions: [PASTEURL],
  patterns: [
    /^https?:\/\/.*\.(jpg|jpeg|gif|png|mp4|webm|webp|avif)([\/\?].+)?$/i,
  ],
},

// Gallery url.
{
  name: "gallery",
  module: "gallery.js",
  actions: [PASTEURL],
  patterns: [
    /^gallery:/,
  ],
},

// Cookie installer.
{
  name: "cookie",
  module: "cookie.js",
  actions: [PASTEURL],
  patterns: [
    /^cookie:/,
  ],
},

// Web articles.
{
  name: "article",
  module: "article.js",
  actions: [SEARCHURL],
  patterns: [
    /^https?:\/\//,
  ],
},

// Data tables.
{
  name: "table",
  module: "table.js",
  actions: [PASTE],
  patterns: [
    /^([\w\s#]+)(\t[\w\s#]+)+\n/,
    /^([\w\s#]+)(,[\w\s#]+)+\n/,
    /^([\w\s#]+)(;[\w\s#]+)+\n/,
    /^([\w\s#]+)(|[\w\s#]+)+\n/,
  ],
},

// RDF (JSON-LD).
{
  name: "rdf",
  module: "rdf.js",
  actions: [PASTE],
  patterns: [
    /^\{\s*\"@context\"/,
  ],
},

];

// Cached HTML pages from html: urls.
var webcache = new Map();

function parse_url(url) {
  try {
    return new URL(url);
  } catch (_) {
    return null;
  }
}

async function load_plugin(module) {
  let module_url = `/case/plugin/${module}`;
  console.log(`Load ${module_url}`);
  try {
    const { default: component } = await import(module_url);
    return component;
  } catch (e) {
    console.log(e);
    throw `error loading ${module}`;
  }
}

class LocalResponse {
  constructor(url, content) {
    this.url = url;
    this.content = content;
  }

  text() {
    return this.content;
  }
}

export class Context {
  constructor(topic, casefile, editor) {
    this.topic = topic;
    this.casefile = casefile;
    this.editor = editor;
    this.select = true;
    this.added = null;
    this.updates = null;
  }

  async new_topic(position) {
    let topic = await this.editor.new_topic(null, position);
    if (!topic) return null;
    this.added = topic;
    if (!this.topic) this.topic = topic;
    return topic;
  }

  updated(topic) {
    if (topic == this.added) return;
    if (!this.updates) this.updates = new Array();
    if (!this.updates.includes(topic)) this.updates.push(topic);
    this.editor.mark_dirty();
  }

  update(topic) {
    this.editor.topic_updated(topic);
    this.editor.update_topic(topic);
  }

  async refresh() {
    if (this.added) {
      this.editor.topic_updated(this.added);
      await this.editor.update_topics();
      if (this.select) {
        await this.editor.navigate_to(this.added);
      }
    } else if (this.updates) {
      for (let topic of this.updates) {
        this.update(topic);
      }
    }
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

  async fetch(url, options = {}) {
    // Try to get content from cache.
    if (webcache.has(url)) {
      return new LocalResponse(url, webcache.get(url));
    }

    // Build headers.
    if (!options.headers) options.headers = {};
    options.headers["XUser-Agent"] = navigator.userAgent;
    let hostname = new URL(url).hostname;
    let cookie = settings.cookiejar[hostname];
    if (cookie) {
      options.headers["XCookie"] = cookie;
    }

    // Fetch content.
    let response = await fetch(this.proxy(url), options);
    if (!response.ok) throw `Error fetching ${url}: ${response.statusText}`;
    return response;
  }

  kblookup(query, params) {
    let qs = new URLSearchParams(params);
    qs.append("q", query);
    qs.append("fmt", "cjson");
    let url = `${settings.kbservice}/kb/query?${qs}`;
    return fetch(url);
  }

  async lookup(name) {
    if (!name) return undefined;
    let r = await this.kblookup(name, {fullmatch: 1});
    let data = await r.json();
    if (data.matches.length > 0) {
      return frame(data.matches[0].ref);
    } else {
      return name;
    }
  }

  async idlookup(prop, identifier) {
    let query = prop.id + "/" + identifier;
    if (this.editor) {
      let topic = this.editor.get_index().ids.get(query);
      if (topic && topic != this.topic) return topic;
    }
    let response = await this.kblookup(query, {fullmatch: 1});
    let result = await response.json();
    if (result.matches.length == 1) {
      let topic = frame(result.matches[0].ref);
      if (topic && topic != this.topic) return topic;
    }
  }
};

export async function process(action, query, context) {
  // Check encoded HTML page.
  if (query.startsWith("html:")) {
    let m = query.match(/^html:([^\s]+) ([^]*)$/);
    if (m) {
      let url = m[1];
      let content = m[2];
      webcache.set(url, content);
      query = url;
    }
  }

  // Check for URL query.
  let url = parse_url(query);
  if (url) {
    if (action == SEARCH) action = SEARCHURL;
    if (action == PASTE) action = PASTEURL;
  } else if (action == SEARCHURL || action == PASTEURL) {
    return false;
  }

  // Try to find plug-in with a matching pattern.
  let result = null;
  for (let plugin of plugins) {
    let match = false;
    if (plugin.actions.includes(action)) {
      for (let pattern of plugin.patterns) {
        if (query.match(pattern)) {
          match = true;
          break;
        }
      }
    }
    if (!match) continue;

    // Load module if not already done.
    if (!plugin.instance) {
      let component = await load_plugin(plugin.module);
      plugin.instance = new component();
    }

    // Let plugin process the query.
    console.log(`Run plugin ${plugin.name} for '${query}'`);
    let r = await plugin.instance.process(action, query, context);
    if (!r) continue;
    if (action == SEARCH || action == SEARCHURL) {
      if (!result) result = new Array();
      result.push(r);
    } else {
      return r;
    }
  }

  return result;
}

export async function search_plugins(context, query, results) {
  let result = await process(SEARCH, query, context);
  if (result instanceof Array) {
    for (let r of result) results.push(r);
  } else if (result) {
    results.push(result);
  }
}

// Topic widgets.
var topic_widgets = [

// Graph widget.
{
  module: "network.js",
  type: frame("Q1900326"),
},

// Timeline widget.
{
  module: "timeline.js",
  type: frame("Q186117"),
},

// Chart widget.
{
  module: "chart.js",
  type: frame("Q28923"),
},

];

const topic_widget_map = new Map(topic_widgets.map(w => [w.type, w]));

const n_instance_of = frame("P31");

export async function get_widget(topic) {
  // Get topic type.
  let type = store.resolve(topic.get(n_instance_of));
  if (!type) return null;

  // Find widget type for topic type.
  let widget = topic_widget_map.get(type);
  if (!widget) return null;

  // Load widget if not already done.
  if (!widget.factory) widget.factory = await load_plugin(widget.module);

  // Return new instance of widget.
  return new widget.factory(topic);
}


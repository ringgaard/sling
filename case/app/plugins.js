// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-ins.

var plugins = [

// Photo albums from Reddit and Imgur.
{
  name: "albums",
  module: "albums.js",
  urls: [
    /^https?:\/\/(i\.|www\|m\.)?imgur.com\//,
    /^https?:\/\/(www\.|old\.)?reddit\.com\//,
    /^https?:\/\/i\.redd\.it\//,
    /^https?:\/\/i\.redditmedia\.com\//,
    /^https?:\/\/preview\.redd\.it\//,
  ],
},

// JPG, GIF and PNG images.
{
  name: "images",
  module: "images.js",
  urls: [
    /^https?:\/\/.*\.(jpg|jpeg|gif|png)(\?.+)?$/,
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
};

export async function process_url(url, context) {
  // Skip if url is invalid.
  let u = parse_url(url);
  if (!u) return false;

  // Try to find plug-in with a matching pattern.
  let plugin = null;
  for (let p of plugins) {
    for (let pattern of p.urls) {
      if (url.match(pattern)) {
        plugin = p;
        break;
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

  // Let plugin process the url.
  console.log(`Run plugin ${plugin.name} for ${url}`);
  return plugin.instance.process(url, context);
}


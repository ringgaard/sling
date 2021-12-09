// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {store} from "./global.js";

const n_is = store.lookup("is");
const n_title = store.lookup("P1476");
const n_homepage = store.lookup("P856");

let xrefs = [
  {
    pattern: /^(mailto:.+)/,
    property: store.lookup("P968"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?twitter\.com\/([A-Za-z0-9_]+)(\?.*)?$/i,
    property: store.lookup("P2002"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?instagram\.com\/([^\/\?]+)/i,
    property: store.lookup("P2003"),
  },
  {
    pattern: /^https?:\/\/www\.facebook\.com\/pg\/([^\?\/]+)/i,
    property: store.lookup("P2013"),
  },
  {
    pattern: /^https?:\/\/www\.facebook\.com\/people\/([^\?\/]+)/i,
    property: store.lookup("P2013"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?facebook\.com\/([^\?\/]+)/i,
    property: store.lookup("P2013"),
  },
  {
    pattern: /^https?:\/\/m\.facebook\.com\/([^\?\/]+)/i,
    property: store.lookup("P2013"),
  },
  {
    pattern: /^https?:\/\/open\.spotify\.com\/artist\/([^\/\?]+)/,
    property: store.lookup("P1902"),
  },
  {
    pattern: /^https?:\/\/vk\.com\/([^\/\?]+)/,
    property: store.lookup("P3185"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?twitch\.tv\/([^\/\?]+)/i,
    property: store.lookup("P5797"),
  },
  {
    pattern: /^https?:\/\/onlyfans\.com\/([^\/\?]+)/i,
    property: store.lookup("P8604"),
  },
  {
    pattern: /^https?:\/\/www\.onlyfans\.com\/([^\/\?]+)/,
    property: store.lookup("P8604"),
  },
  {
    pattern: /^https?:\/\/discord\.gg\/([^\/\?]+)/,
    property: store.lookup("P9101"),
  },
  {
    pattern: /^https?:\/\/linkedin\.com\/in\/([^\/\?]+)/,
    property: store.lookup("P6634"),
  },
  {
    pattern: /^https?:\/\/\w+\.linkedin\.com\/in\/([^\/\?]+)/,
    property: store.lookup("P6634"),
  },
  {
    pattern: /^https?:\/\/linkedin\.com\/company\/([^\/\?]+)/,
    property: store.lookup("P4264"),
  },
  {
    pattern: /^https?:\/\/\w+\.linkedin\.com\/company\/([^\/\?]+)/,
    property: store.lookup("P4264"),
  },
  {
    pattern: /^https?:\/\/t\.me\/([^\/\?]+)/,
    property: store.lookup("P3789"),
  },
  {
    pattern: /^https?:\/\/vimeo\.com\/([^\/\?]+)/,
    property: store.lookup("P4015"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?youtube\.com\/channel\/([^\/\?]+)/i,
    property: store.lookup("P2397"),
  },
  {
    pattern: /^https?:\/\/www\.youtube\.com\/playlist\?list=([^\/\?]+)/,
    property: store.lookup("P4300"),
  },
  {
    pattern: /^https?:\/\/youtu.be\/([^\/\?]+)/,
    property: store.lookup("P1651"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?imdb\.com\/name\/([^\/\?]+)/i,
    property: store.lookup("P345"),
  },
  {
    pattern: /^https?:\/\/m\.imdb\.com\/name\/([^\/\?]+)/i,
    property: store.lookup("P345"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?patreon\.com\/([^\/\?]+)/i,
    property: store.lookup("P4175"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?tiktok\.com\/@([^\/\?]+)/i,
    property: store.lookup("P7085"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?cameo\.com\/([^\/\?]+)/i,
    property: store.lookup("P6908"),
  },
  {
    pattern: /^https?:\/\/([^\s\/]+)\.tumblr\.com\/?/i,
    property: store.lookup("P3943"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?myspace\.com\/([^\/\?]+)/,
    property: store.lookup("P3265"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?reddit\.com\/r\/([^\/\?]+)\/?$/,
    property: store.lookup("P3984"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?reddit\.com\/user\/([^\/\?]+)\/?$/,
    property: store.lookup("P4265"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?snapchat\.com\/add\/([^\/\?]+)\/?$/,
    property: store.lookup("P2984"),
  },
  {
    pattern: /^(https?:\/\/(?:www\.)?bellazon\.com\/main\/topic\/.+)/i,
    property: store.lookup("P973"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?playboyplus\.com\/profile\/(.+)/,
    property: store.lookup("P5346"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?models\.com\/models\/(.+)/,
    property: store.lookup("P2471"),
  },
  {
    pattern: /^https?:\/\/www\.iafd\.com\/person\.rme\/perfid=(\w+)\/?/,
    property: store.lookup("P3869"),
  },
  {
    pattern: /^https?:\/\/scholar\.google\.com\/citations\/?\?user=(\w+)/,
    property: store.lookup("P1960"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?researchgate\.net\/profile\/(.+)/,
    property: store.lookup("P2038"),
  },
  {
    pattern: /^https?:\/\/academic\.microsoft\.com\/author\/(\d+)/,
    property: store.lookup("P6366"),
  },
];

export function match_link(url) {
  for (let xref of xrefs) {
    let m = url.match(xref.pattern);
    if (m) {
      let prop = xref.property;
      let identifier = decodeURIComponent(m[1]);
      return [prop, identifier];
    }
  }
  return [null, null];
}

export function xref_patterns() {
  let patterns = [];
  for (let xref of xrefs) {
    patterns.push(xref.pattern);
  }
  return patterns;
}

export class SocialTopic {
  constructor(topic, context) {
    this.topic = topic;
    this.context = context;
  }

  async add_link(url, title) {
    let [prop, identifier] = match_link(url);

    if (prop) {
      if (!this.topic.has(prop, identifier)) {
        this.topic.add(prop, identifier);

        if (this.context) {
          let item = await this.context.idlookup(prop, identifier);
          if (item) this.topic.put(n_is, item);
        }
        return true;
      }
    } else {
      if (!this.topic.has(n_homepage, url)) {
        if (title) {
          let q = store.frame();
          q.add(n_is, url);
          q.add(n_title, title);
          this.topic.add(n_homepage, q);
        } else {
          this.topic.add(n_homepage, url);
        }
        return true;
      }
    }
    return false;
  }
};

const emojis = /([\u2700-\u27BF]|[\uE000-\uF8FF]|\uD83C[\uDC00-\uDFFF]|\uD83D[\uDC00-\uDFFF]|[\u2011-\u26FF]|\uD83E[\uDD10-\uDDFF])/g;

export function strip_emojis(s) {
  return s.replace(emojis, "").trim();
}


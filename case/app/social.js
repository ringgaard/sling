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
    pattern: /^https?:\/\/[Tt]witter\.com\/([A-Za-z_]+)(\?.*)?$/,
    property: store.lookup("P2002"),
  },
  {
    pattern: /^https?:\/\/[Ii]nstagram\.com\/([^\/\?]+)/,
    property: store.lookup("P2003"),
  },
  {
    pattern: /^https?:\/\/www\.instagram\.com\/([^\/\?]+)/,
    property: store.lookup("P2003"),
  },
  {
    pattern: /^https?:\/\/[[Ff]acebook\.com\/([^\/\?]+)/,
    property: store.lookup("P2013"),
  },
  {
    pattern: /^https?:\/\/www\.facebook\.com\/([^\/\?]+)/,
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
    pattern: /^https?:\/\/[Tt]witch\.tv\/([^\/\?]+)/,
    property: store.lookup("P5797"),
  },
  {
    pattern: /^https?:\/\/www\.twitch\.tv\/([^\/\?]+)/,
    property: store.lookup("P5797"),
  },
  {
    pattern: /^https?:\/\/[Oo]nlyfans\.com\/([^\/\?]+)/,
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
    pattern: /^https?:\/\/youtube\.com\/channel\/([^\/\?]+)/,
    property: store.lookup("P2397"),
  },
  {
    pattern: /^https?:\/\/www\.youtube\.com\/channel\/([^\/\?]+)/,
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
    pattern: /^https?:\/\/imdb\.com\/name\/([^\/\?]+)/,
    property: store.lookup("P345"),
  },
  {
    pattern: /^https?:\/\/m\.imdb\.com\/name\/([^\/\?]+)/,
    property: store.lookup("P345"),
  },
  {
    pattern: /^https?:\/\/[Pp]atreon\.com\/([^\/\?]+)/,
    property: store.lookup("P4175"),
  },
  {
    pattern: /^https?:\/\/www\.patreon\.com\/([^\/\?]+)/,
    property: store.lookup("P4175"),
  },
  {
    pattern: /^https?:\/\/tiktok\.com\/@([^\/\?]+)/,
    property: store.lookup("P7085"),
  },
  {
    pattern: /^https?:\/\/cameo\.com\/([^\/\?]+)/,
    property: store.lookup("P6908"),
  },
  {
    pattern: /^https?:\/\/www\.cameo\.com\/([^\/\?]+)/,
    property: store.lookup("P6908"),
  },
];

export class SocialTopic {
  constructor(topic) {
    this.topic = topic;
  }

  add_link(url, title) {
    let prop = null;
    let identifier = null;
    for (let xref of xrefs) {
      let m = url.match(xref.pattern);
      if (m) {
        prop = xref.property;
        identifier = m[1];
        break;
      }
    }
    if (prop) {
      if (!this.topic.has(prop, identifier)) {
        this.topic.add(prop, identifier);
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


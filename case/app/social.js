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
    pattern: /^https:\/\/ringgaard.com\/kb\/(.*)$/i,
    property: store.lookup("is"),
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
    pattern: /^https?:\/\/(?:www\.).pinterest\.\w+\/([^\/\?]+)/,
    property: store.lookup("P3836"),
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
    pattern: /^https?:\/\/(?:www\.|m\.)?youtube\.com\/channel\/([^\/\?]+)/i,
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
    pattern: /^https?:\/\/(?:www\.)?themoviedb\.org\/person\/(\d+)/i,
    property: store.lookup("P4985"),
  },
  {
    pattern: /^https?:\/\/filmpolski\.pl\/fp\/index\.php(?:\?osoba=|\/)(\d+)/i,
    property: store.lookup("P3495"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?filmweb\.pl\/person\/.+-(\d+)/i,
    property: store.lookup("P5033"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?dfi\.dk\/viden-om-film\/filmdatabasen\/person\/(\w+)/i,
    property: store.lookup("P2626"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?danskefilm\.dk\/skuespiller\.php\?id=(\d+)/i,
    property: store.lookup("P3786"),
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
    pattern: /^https?:\/\/medium\.com\/@([^\/\?]+)/i,
    property: store.lookup("P3899"),
  },
  {
    pattern: /https?:\/\/muckrack\.com\/([^\/]+)\/?/i,
    property: store.lookup("P6005"),
  },
  {
    pattern: /^https?:\/\/pinterest\.com\/([^\/\?]+)/i,
    property: store.lookup("P3836"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?cameo\.com\/([^\/\?]+)/i,
    property: store.lookup("P6908"),
  },
  {
    pattern: /^https?:\/\/([^\s\/]+)\.tumblr\.com\/?$/i,
    property: store.lookup("P3943"),
  },
  {
    pattern: /^https?:\/\/([^\s\/]+)\.blogspot\.com\/?/i,
    property: store.lookup("P8772"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?myspace\.com\/([^\/\?]+)/,
    property: store.lookup("P3265"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?vimeo\.com\/([^\/\?]+)/,
    property: store.lookup("P4015"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?soundcloud\.com\/([^\/\?]+)/,
    property: store.lookup("P3040"),
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
    pattern: /^http:\/\/(?:www\.)?vintage-erotica-forum\.com\/showthread\.php\?t=(\d+)/,
    property: store.lookup("PVEF"),
  },
  {
    pattern: /^http:\/\/(?:www\.)?vintage-erotica-forum\.com\/t(\d+)-/,
    property: store.lookup("PVEF"),
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
    pattern: /^https?:\/\/www\.adultfilmdatabase\.com\/actor.cfm\?actorid=(\w+)/,
    property: store.lookup("P3351"),
  },
  {
    pattern: /^https?:\/\/www\.egafd\.com\/actresses\/details.php\/id\/(\w+)/,
    property: store.lookup("P8767"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?boobpedia\.com\/boobs\/(.+)/,
    property: store.lookup("PBOOB"),
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
  {
    pattern: /^https?:\/\/github\.com\/([A-Za-z0-9\-]+)/,
    property: store.lookup("P2037"),
  },
  {
    pattern: /^https?:\/\/dblp\.org\/pid\/(\w+\/\w+)\.html/,
    property: store.lookup("P2456"),
  },
  {
    pattern: /^https?:\/\/orcid\.org\/([0-9A-Z\-]+)/,
    property: store.lookup("P496"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?worldathletics\.org\/athletes\/[^\/]+\/(?:[^?\/]+-)?(\d+)/,
    property: store.lookup("P1146"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?the-sports\.org\/.+-spf(\d+).html/,
    property: store.lookup("P4391"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?fina\.org\/athletes\/(\d+)/,
    property: store.lookup("P3408"),
  },
  {
    pattern: /^https?:\/\/muckrack\.com\/(.+)/,
    property: store.lookup("P6005"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?blogger\.com\/profile\/(\d+)/,
    property: store.lookup("P8772"),
  },
  {
    pattern: /^https?:\/\/mubi\.com\/cast\/(.+)/,
    property: store.lookup("P7300"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?findagrave\.com\/memorial\/(\d+)/,
    property: store.lookup("P535"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?geni\.com\/people\/.+\/(\d+)/,
    property: store.lookup("P2600"),
  },
  {
    pattern: /^(https?:\/\/honeydrip\.com\/model\/.+)/,
    property: store.lookup("P973"),
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
        this.topic.put(prop, identifier);

        if (this.context) {
          let item = await this.context.idlookup(prop, identifier);
          if (item) this.topic.put(n_is, item.id);
        }
        return true;
      }
    } else {
      if (!this.topic.has(n_homepage, url)) {
        if (title) {
          let q = store.frame();
          q.put(n_is, url);
          q.put(n_title, title);
          this.topic.put(n_homepage, q);
        } else {
          this.topic.put(n_homepage, url);
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


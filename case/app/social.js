// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {store, frame} from "/common/lib/global.js";

const n_is = frame("is");
const n_title = frame("P1476");
const n_homepage = frame("P856");

let xrefs = [
  {
    pattern: /^(mailto:.+)/,
    property: frame("P968"),
  },
  {
    pattern: /^https:\/\/ringgaard.com\/kb\/(.*)$/i,
    property: frame("is"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?twitter\.com\/([A-Za-z0-9_]+)(\?.*)?$/i,
    property: frame("P2002"),
  },
  {
    pattern: /^https?:\/\/(?:mobile\.)?twitter\.com\/([A-Za-z0-9_]+)(\?.*)?$/i,
    property: frame("P2002"),
  },
  {
    pattern: /^https?:\/\/(?:mobile\.)?x\.com\/([A-Za-z0-9_]+)(\?.*)?$/i,
    property: frame("P2002"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?instagram\.com\/([A-Za-z0-9_\.)]+)\/?(\?.*)?$/i,
    property: frame("P2003"),
  },
  {
    pattern: /^https?:\/\/(?:(:?www|m)\.)facebook\.com\/pg\/([^\?\/]+)/i,
    property: frame("P2013"),
  },
  {
    pattern: /^https?:\/\/(?:(?:www|m)\.)facebook\.com\/people\/([^\?\/]+)\/(\w+)/,
    property: frame("P2013"),
    extract: m => decodeURIComponent(m[1]) + "-" + decodeURIComponent(m[2])
  },
  {
    pattern: /^https?:\/\/www\.facebook\.com\/profile\.php\?id=(\d+)/,
    property: frame("P2013"),
  },
  {
    pattern: /^https?:\/\/(?:[a-z\-]+\.)?facebook\.com\/([^\?\/]+)/i,
    property: frame("P2013"),
  },
  {
    pattern: /^https?:\/\/open\.spotify\.com\/artist\/([^\/\?]+)/,
    property: frame("P1902"),
  },
  {
    pattern: /^https?:\/\/(?:music)\.apple\.com\/(?:\w+)\/artist\/(?:\w+)\/(\d+)/,
    property: frame("P2850"),
  },
  {
    pattern: /^https?:\/\/www\.discogs\.com\/(?:[a-z]+\/)?artist\/([1-9][0-9]*)/,
    property: frame("P1953"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?pinterest\.\w+\/([^\/\?]+)/,
    property: frame("P3836"),
  },
  {
    pattern: /^https?:\/\/vk\.com\/([^\/\?]+)/,
    property: frame("P3185"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?twitch\.tv\/([^\/\?]+)/i,
    property: frame("P5797"),
  },
  {
    pattern: /^https?:\/\/onlyfans\.com\/([^\/\?]+)/i,
    property: frame("P8604"),
  },
  {
    pattern: /^https?:\/\/www\.onlyfans\.com\/([^\/\?]+)/,
    property: frame("P8604"),
  },
  {
    pattern: /^https?:\/\/discord\.gg\/([^\/\?]+)/,
    property: frame("P9101"),
  },
  {
    pattern: /^https?:\/\/linkedin\.com\/in\/([^\/\?]+)/,
    property: frame("P6634"),
  },
  {
    pattern: /^https?:\/\/\w+\.linkedin\.com\/in\/([^\/\?]+)/,
    property: frame("P6634"),
  },
  {
    pattern: /^https?:\/\/linkedin\.com\/company\/([^\/\?]+)/,
    property: frame("P4264"),
  },
  {
    pattern: /^https?:\/\/\w+\.linkedin\.com\/company\/([^\/\?]+)/,
    property: frame("P4264"),
  },
  {
    pattern: /^https?:\/\/t\.me\/([^\/\?]+)/,
    property: frame("P3789"),
  },
  {
    pattern: /^https?:\/\/vimeo\.com\/([^\/\?]+)/,
    property: frame("P4015"),
  },
  {
    pattern: /^https?:\/\/(?:www\.|m\.)?youtube\.com\/channel\/([^\/\?]+)/i,
    property: frame("P2397"),
  },
  {
    pattern: /^https?:\/\/www\.youtube\.com\/playlist\?list=([^\/\?]+)/,
    property: frame("P4300"),
  },
  {
    pattern: /^https?:\/\/youtu.be\/([^\/\?]+)/,
    property: frame("P1651"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?imdb\.com\/name\/([^\/\?]+)/i,
    property: frame("P345"),
  },
  {
    pattern: /^https?:\/\/m\.imdb\.com\/name\/([^\/\?]+)/i,
    property: frame("P345"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?themoviedb\.org\/person\/(\d+)/i,
    property: frame("P4985"),
  },
  {
    pattern: /^https?:\/\/filmpolski\.pl\/fp\/index\.php(?:\?osoba=|\/)(\d+)/i,
    property: frame("P3495"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?filmweb\.pl\/person\/.+-(\d+)/i,
    property: frame("P5033"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?dfi\.dk\/viden-om-film\/filmdatabasen\/person\/(\w+)/i,
    property: frame("P2626"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?danskefilm\.dk\/skuespiller\.php\?id=(\d+)/i,
    property: frame("P3786"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?patreon\.com\/([^\/\?]+)/i,
    property: frame("P4175"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?tiktok\.com\/@([^\/\?]+)/i,
    property: frame("P7085"),
  },
  {
    pattern: /^https?:\/\/medium\.com\/@([^\/\?]+)/i,
    property: frame("P3899"),
  },
  {
    pattern: /^https?:\/\/pinterest\.com\/([^\/\?]+)/i,
    property: frame("P3836"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?cameo\.com\/([^\/\?]+)/i,
    property: frame("P6908"),
  },
  {
    pattern: /^https?:\/\/([^\s\/]+)\.tumblr\.com\/?$/i,
    property: frame("P3943"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?myspace\.com\/([^\/\?]+)/,
    property: frame("P3265"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?vimeo\.com\/([^\/\?]+)/,
    property: frame("P4015"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?soundcloud\.com\/([^\/\?]+)/,
    property: frame("P3040"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?reddit\.com\/r\/([^\/\?]+)\/?$/,
    property: frame("P3984"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?reddit\.com\/user\/([^\/\?]+)\/?$/,
    property: frame("P4265"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?snapchat\.com\/add\/([^\/\?]+)\/?$/,
    property: frame("P2984"),
  },
  {
    pattern: /^(https?:\/\/(?:www\.)?bellazon\.com\/main\/topic\/.+)/i,
    property: frame("P973"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?playboyplus\.com\/profile\/(.+)/,
    property: frame("P5346"),
  },
  {
    pattern: /^http:\/\/(?:www\.)?vintage-erotica-forum\.com\/showthread\.php\?t=(\d+)/,
    property: frame("PVEF"),
  },
  {
    pattern: /^http:\/\/(?:www\.)?vintage-erotica-forum\.com\/t(\d+)-/,
    property: frame("PVEF"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?models\.com\/models\/(.+)/,
    property: frame("P2471"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?models\.com\/people\/(.+)/,
    property: frame("P2471"),
  },
  {
    pattern: /^https?:\/\/www\.adultfilmdatabase\.com\/actor.cfm\?actorid=(\w+)/,
    property: frame("P3351"),
  },
  {
    pattern: /^https?:\/\/www\.adultfilmdatabase\.com\/actor\/[A-Za-z-]+-(\d+)/,
    property: frame("P3351"),
  },
  {
    pattern: /^https?:\/\/www\.egafd\.com\/actresses\/details.php\/id\/(\w+)/,
    property: frame("P8767"),
  },
  {
    pattern: /^https?:\/\/www\.bgafd\.co\.uk\/actresses\/details.php\/id\/(\w+)/,
    property: frame("PBGAFD"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?boobpedia\.com\/boobs\/(.+)/,
    property: frame("PBOOB"),
  },
  {
    pattern: /^https?:\/\/scholar\.google\.com\/citations\/?\?user=(\w+)/,
    property: frame("P1960"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?researchgate\.net\/profile\/(.+)/,
    property: frame("P2038"),
  },
  {
    pattern: /^https?:\/\/academic\.microsoft\.com\/author\/(\d+)/,
    property: frame("P6366"),
  },
  {
    pattern: /^https?:\/\/github\.com\/([A-Za-z0-9\-]+)/,
    property: frame("P2037"),
  },
  {
    pattern: /^https?:\/\/dblp\.org\/pid\/(\w+\/\w+)\.html/,
    property: frame("P2456"),
  },
  {
    pattern: /^https?:\/\/orcid\.org\/([0-9A-Z\-]+)/,
    property: frame("P496"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?worldathletics\.org\/athletes\/[^\/]+\/(?:[^?\/]+-)?(\d+)/,
    property: frame("P1146"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?the-sports\.org\/.+-spf(\d+).html/,
    property: frame("P4391"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?fina\.org\/athletes\/(\d+)/,
    property: frame("P3408"),
  },

  {
    pattern: /^https?:\/\/(?:www\.)?itftennis\.com\/en\/players\/([a-z-]+\/\d+\/\w+)/,
    property: frame("P8618"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?wtatennis\.com\/players\/(\d+)/,
    property: frame("P597"),
  },
  {
    pattern: /https?:\/\/muckrack\.com\/([^\/]+)\/?/i,
    property: frame("P6005"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?blogger\.com\/profile\/(\d+)/,
    property: frame("P8772"),
  },
  {
    pattern: /^https?:\/\/mubi\.com\/cast\/(.+)/,
    property: frame("P7300"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?findagrave\.com\/memorial\/(\d+)/,
    property: frame("P535"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?geni\.com\/people\/.+\/(\d+)/,
    property: frame("P2600"),
  },
  {
    pattern: /^(https?:\/\/honeydrip\.com\/model\/.+)/,
    property: frame("P973"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?fashionmodeldirectory\.com\/models\/(.+?)(\/|\?|$)/,
    property: frame("P2266"),
  },
  {
    pattern: /^https?:\/\/linktr\.ee\/(.+)/,
    property: frame("P11079"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?crunchbase\.com\/person\/([^#^\/]+)/,
    property: frame("P2087"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?crunchbase\.com\/organization\/([^#^\/]+)/,
    property: frame("P2088"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)?wikitree\.com\/wiki\/([^#^\/]+)/,
    property: frame("P2949"),
  },
  {
    pattern: /^https?:\/\/(?:www\.)ibdb\.com\/broadway-cast-staff\/[a-z-]+(\d+)/,
    property: frame("P1220"),
  },
  {
    pattern: /^https?:\/\/([a-z0-9-]+)\.fandom\.com\/wiki\/([^#^\/]+)/,
    property: frame("P6262"),
    extract: m => decodeURIComponent(m[1]) + ":" + decodeURIComponent(m[2])
  },
  {
    pattern: /^https?:\/\/mypmates\.club\/((?!model).+)/,
    property: frame("PMYP"),
  },
  {
    pattern: /^https?:\/\/myppets\.club\/((?!img).+)/,
    property: frame("PMYPP"),
  },
];

export function match_link(url) {
  for (let xref of xrefs) {
    let m = url.match(xref.pattern);
    if (m) {
      let prop = xref.property;
      let id = xref.extract ? xref.extract(m) : decodeURIComponent(m[1]);
      return [prop, id];
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

  async add_link(url, title, unknown) {
    let [prop, identifier] = match_link(url);

    if (prop) {
      if (!this.topic.has(prop, identifier)) {
        this.topic.put(prop, identifier);

        if (this.context) {
          let item = await this.context.idlookup(prop, identifier);
          if (item && item != this.topic) this.topic.put(n_is, item.id);
        }
        return true;
      }
    } else if (unknown) {
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

  async add_prop(prop, identifier) {
    this.topic.put(prop, identifier);
    if (this.context) {
      let item = await this.context.idlookup(prop, identifier);
      if (item && item != this.topic) this.topic.put(n_is, item.id);
    }
  }
};

const emojis = /([\u2700-\u27BF]|[\uE000-\uF8FF]|\uD83C[\uDC00-\uDFFF]|\uD83D[\uDC00-\uDFFF]|[\u2011-\u26FF]|\uD83E[\uDD10-\uDDFF])/g;

export function strip_emojis(s) {
  return s.replace(emojis, "").trim();
}


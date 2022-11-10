// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for biographies from imdb.com.

import {store} from "/case/app/global.js";
import {date_parser} from "/case/app/value.js";
import {SocialTopic, strip_emojis} from "/case/app/social.js";
import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";

const n_name = store.lookup("name");
const n_birth_name = store.lookup("P1477");
const n_nickname = store.lookup("P1449");
const n_instance_of = store.lookup("P31");
const n_human = store.lookup("Q5");
const n_date_of_birth = store.lookup("P569");
const n_place_of_birth = store.lookup("P19");
const n_date_of_death = store.lookup("P570");
const n_place_of_death = store.lookup("P20");
const n_cause_of_death = store.lookup("P509");
const n_country_of_citizenship = store.lookup("P27");
const n_height = store.lookup("P2048");
const n_cm = store.lookup("Q174728");
const n_amount = store.lookup("/w/amount");
const n_unit = store.lookup("/w/unit");
const n_start_time = store.lookup("P580");
const n_end_time = store.lookup("P582");
const n_imdb = store.lookup("P345");

const n_parent = store.lookup("P8810");
const n_sibling = store.lookup("P3373");
const n_relative = store.lookup("P1038");
const n_end_cause = store.lookup("P1534");
const n_divorce = store.lookup("Q93190");
const n_kinship = store.lookup("P1039");

const kinships = {
  "Parents": {prop: n_parent},
  "Spouse": {prop: store.lookup("P26")},
  "Children": {prop: store.lookup("P40")},
  "Relatives": {prop: n_relative},

  "sibling": {prop: n_sibling},
  "half sibling": {prop: n_sibling, qual: store.lookup("Q27965041")},
  "cousin": {prop: n_relative, qual: store.lookup("Q23009870")},
  "grandparent": {prop: n_relative, qual: store.lookup("Q167918")},
  "grandfather": {prop: n_relative, qual: store.lookup("Q9238344")},
  "grandmother": {prop: n_relative, qual: store.lookup("Q9235758")},
  "aunt or uncle": {prop: n_relative, qual: store.lookup("Q21073936")},
  "aunt": {prop: n_relative, qual: store.lookup("Q76507")},
  "uncle": {prop: n_relative, qual: store.lookup("Q76557")},
  "niece or nephew": {prop: n_relative, qual: store.lookup("Q76477")},
  "niece": {prop: n_relative, qual: store.lookup("Q3403377")},
  "nephew": {prop: n_relative, qual: store.lookup("Q15224724")},
};

async function lookup(context, name) {
  if (!name) return undefined;
  let r = await context.kblookup(name, {fullmatch: 1});
  let data = await r.json();
  if (data.matches.length > 0) {
    return store.lookup(data.matches[0].ref);
  } else {
    return name;
  }
}

function trim(s) {
  return s.replace(/[\n\u00a0]/g, " ").replace(/\s\s+/g, " ").trim()
}

function parse_date(d) {
  if (!d) return undefined;
  let results = new Array();
  date_parser(d, results);
  if (results.length > 0) return results[0].value;
  return d;
}

async function parse_date_and_place(context, text) {
  let m = text.match(/^(.+)\sin\s(.*)\s+\(((.*))\)/);
  if (!m) m = text.match(/(.+)\sin\s(.*)/);
  if (!m) m = text.match(/(.+)/);

  let date = parse_date(m[1]);

  let location, country;
  if (m[2]) {
    location = m[2].split(/, /);
    country = location.pop();
    location = await lookup(context, location.join(", "));
    country = await lookup(context, country);
  }

  let cause = await lookup(context, m[3]);

  return {date, location, country, cause};
}

export default class IMDBPlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let m = url.pathname.match(/^\/name\/(nm\d+)/);
    if (!m) return;
    let imdbid = decodeURIComponent(m[1]);
    if (!imdbid) return;

    if (action == SEARCHURL) {
      return {
        ref: imdbid,
        name: imdbid,
        description: "IMDB actor biography",
        url: url.href,
        context: context,
        onitem: item => this.select(item),
      };
    } else if (action == PASTEURL) {
      await this.populate(context, context.topic, imdbid);
      return true;
    }
  }

  async select(item) {
    // Create new topic.
    let topic = await item.context.new_topic();
    if (!topic) return;

    // Fetch profile from IMDB and populate topic.
    await this.populate(item.context, topic, item.ref);
  }

  async populate(context, topic, imdbid) {
    // Retrieve IMDB biography.
    let url = `https://www.imdb.com/name/${imdbid}/bio`;
    let r = await fetch(context.proxy(url));
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");

    // Get name from title.
    let title = doc.getElementsByTagName("h3")[0];
    let name = title.innerText.trim();

    // Get basic information from overview table.
    let bio = {}
    let overview = doc.getElementById("overviewTable");
    for (let row of overview.querySelectorAll("tr").values()) {
      let cols = row.querySelectorAll("td");
      let label = cols[0].innerText;
      let value = trim(cols[1].innerText);
      if (label == "Born") {
        bio.birth = await parse_date_and_place(context, value);
      } else if (label == "Died") {
        bio.death = await parse_date_and_place(context, value);
      } else if (label == "Birth Name") {
        bio.birthname = value;
      } else if (label == "Nickname") {
        bio.nickname = value;
      } else if (label == "Height") {
        let m = value.match(/^\d+'(\s\d+[¼½¾]?")\s+\((\d)\.(\d+) m\)/);
        if (m) {
          let v = store.frame();
          let height = parseInt(m[3]);
          if (m[3].length == 1) height *= 10;
          height += parseInt(m[2]) * 100;
          v.add(n_amount, height);
          v.add(n_unit, n_cm);
          if (!topic.has(n_height)) bio.height = v
        }
      } else {
        console.log("unknown overview item", label, value);
      }
    }

    // Add bio to topic.
    topic.put(n_name, name);
    topic.put(n_birth_name, bio.birthname);
    topic.put(n_nickname, bio.nickname);
    topic.put(n_instance_of, n_human);
    if (bio.birth) {
      topic.put(n_date_of_birth, bio.birth.date);
      topic.put(n_place_of_birth, bio.birth.location);
    }
    if (bio.death) {
      topic.put(n_date_of_death, bio.death.date);
      topic.put(n_place_of_death, bio.death.location || bio.death.country);
      topic.put(n_cause_of_death, bio.death.cause);
    }
    if (bio.birth) topic.put(n_country_of_citizenship, bio.birth.country);

    // Add family to topic.
    let family = doc.getElementById("tableFamily");
    if (family) {
      for (let row of family.querySelectorAll("tr").values()) {
        let cols = row.querySelectorAll("td");
        let label = cols[0].innerText.trim();
        let value = cols[1];

        // Get kinship from label.
        let kinship = kinships[label];
        if (!kinship) {
          console.log("Unknown kinship: ", label);
          continue;
        }

        // Parse text and links.
        let links = new Map();
        let lines = [];
        let line = "";
        for (let c = value.firstChild; c; c = c.nextSibling) {
          if (c.nodeType == Node.TEXT_NODE) {
            line += c.textContent;
          } else if (c.nodeType == Node.ELEMENT_NODE) {
            if (c.tagName == "A") {
              let href = c.getAttribute("href");
              let m = href.match(/^\/name\/(nm\d+)/);
              let text = c.innerText;
              if (m) links.set(text.trim(), m[1]);
              line += text;
            } else if (c.tagName == "BR") {
              lines.push(trim(line));
              line = "";
            } else {
              console.log(c.tagName);
            }
          }
        }
        if (line) lines.push(trim(line));

        // Add family member(s).
        for (let l of lines) {
          // Get name, period, relation, and cause.
          let name, start, end, relation, cause;
          let m = l.match(/^(.+?) \((.+?) - (.+?)\)( \((.+?)\))?/)
          if (m) {
            name = m[1];
            start = m[2];
            end = m[3];
            relation = m[5];
          } else {
            let m = l.match(/^(.+?) \((.+?)\)/);
            if (m) {
              name = m[1];
              relation = m[2];
            } else {
              name = l;
            }
          }
          if (relation in kinships) kinship = kinships[relation];
          if (relation == "divorced") cause = n_divorce;
          if (start == "?") start = undefined;
          if (end == "?" || end == "present") end = undefined;

          // Try to resolve name.
          let relative = name;
          let link = links.get(name);
          if (link) {
            let item = await context.idlookup(n_imdb, link);
            if (item) {
              relative = item;
              link = null;
            }
          }

          // Add family member.
          if (start || end || cause || link || kinship.qual) {
            let v = store.frame();
            v.add(store.is, relative);
            if (start) v.add(n_start_time, parse_date(start));
            if (end) v.add(n_end_time, parse_date(end));
            if (cause) v.add(n_end_cause, cause);
            if (kinship.qual) v.add(n_kinship, kinship.qual);
            if (link) v.add(n_imdb, link);
            topic.add(kinship.prop, v);
          } else {
            topic.put(kinship.prop, relative);
          }
        }
      }
    }

    topic.put(n_height, bio.height);

    // Add IMDB id.
    topic.put(n_imdb, imdbid);
    let item = await context.idlookup(n_imdb, imdbid);
    if (item) topic.put(store.is, item);

    context.updated(topic);
  }
};


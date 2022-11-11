// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for biographies from imdb.com.

import {store} from "/case/app/global.js";
import {date_parser, ItemCollector} from "/case/app/value.js";
import {match_link} from "/case/app/social.js";
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
const n_father = store.lookup("P22");
const n_mother = store.lookup("P25");
const n_sibling = store.lookup("P3373");
const n_relative = store.lookup("P1038");
const n_end_cause = store.lookup("P1534");
const n_divorce = store.lookup("Q93190");
const n_kinship = store.lookup("P1039");

const n_gender = store.lookup("P21");
const n_female = store.lookup("Q6581072");
const n_male = store.lookup("Q6581097");

const kinships = {
  "Parents": {prop: n_parent, male: "father", female: "mother", rank: 13},
  "father": {prop: n_father, gender: n_male, rank: 11},
  "mother": {prop: n_mother, gender: n_female, rank: 12},

  "sibling": {prop: n_sibling, rank: 20},
  "half sibling": {prop: n_sibling, qual: store.lookup("Q27965041"), rank: 21},

  "Spouse": {prop: store.lookup("P26"), rank: 40},
  "Children": {prop: store.lookup("P40"), rank: 50},

  "Relatives": {prop: n_relative, rank: 1000},

  "grandparent": {
    prop: n_relative,
    qual: store.lookup("Q167918"),
    male: "grandfather",
    female: "grandmother",
    rank: 102,

  },
  "grandfather": {prop: n_relative, qual: store.lookup("Q9238344"), rank: 101},
  "grandmother": {prop: n_relative, qual: store.lookup("Q9235758"), rank: 102},

  "great grandparent": {
    prop: n_relative,
    qual: store.lookup("Q2500619"),
    male: "great grandfather",
    female: "great grandmother",
    rank: 106,
  },
  "great grandfather": {
    prop: n_relative,
    qual: store.lookup("Q2500621"),
    rank: 104,
  },
  "great grandmother": {
    prop: n_relative,
    qual: store.lookup("Q2500620"),
    rank: 105,
  },

  "aunt or uncle": {
    prop: n_relative,
    qual: store.lookup("Q21073936"),
    male: "uncle",
    female: "aunt",
    rank: 113,
  },
  "uncle": {prop: n_relative, qual: store.lookup("Q76557"), rank: 111},
  "aunt": {prop: n_relative, qual: store.lookup("Q76507"), rank: 112},

  "niece or nephew": {
    prop: n_relative,
    qual: store.lookup("Q76477"),
    male: "nephew",
    female: "niece",
    rank: 123,
  },
  "nephew": {prop: n_relative, qual: store.lookup("Q15224724"), rank: 121},
  "niece": {prop: n_relative, qual: store.lookup("Q3403377"), rank: 122},

  "cousin": {prop: n_relative, qual: store.lookup("Q23009870"), rank: 130},
};

const gender_words = {
  "she": n_female,
  "her": n_female,
  "actress": n_female,
  "he": n_male,
  "his": n_male,
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

var name_gender;

async function gender_for_name(name) {
  if (!name_gender) {
    name_gender = new Map();
    try {
      let r = await fetch("https://ringgaard.com/static/name-gender.json");
      let gender_table = await r.json();
      for (let name of gender_table.male) name_gender.set(name, n_male);
      for (let name of gender_table.female) name_gender.set(name, n_female);
      console.log(`${name_gender.size} gendered first names loaded`);
    } catch (error) {
      console.log("failed to load name gender table", error);
    }
  }
  let names = name.split(' ');
  if (names.length > 0) return name_gender.get(names[0]);
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
    if (overview) {
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
        } else if (label == "Nicknames") {
          // Skip if multiple nicknames.
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
    }

    // Guess gender from bio text.
    let male_score = 0;
    let female_score = 0;
    for (let block of doc.querySelectorAll("div.soda").values()) {
      let text = trim(block.innerText).toLowerCase();
      let tokens = text.split(/[\s\(\)\.,:&\-"]+/);
      for (let token of tokens) {
        let gender = gender_words[token];
        if (gender) {
          if (gender == n_male) male_score += 1;
          if (gender == n_female) female_score += 1;
        }
      }
    }
    console.log("male", male_score, "female", female_score);
    if (male_score || female_score) {
      bio.gender =  male_score > female_score ? n_male : n_female;
    }
    if (!bio.gender) bio.gender = await gender_for_name(name);

    // Add bio to topic.
    topic.put(n_name, name);
    topic.put(n_birth_name, bio.birthname);
    topic.put(n_nickname, bio.nickname);
    topic.put(n_instance_of, n_human);
    topic.put(n_gender, bio.gender);
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

    // Get relatives.
    let relatives = new Array();
    let collector = new ItemCollector(store);
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
          let r = {kinship}
          let m = l.match(/^(.+?) \((.+?) - (.+?)\)( \((.+?)\))?/)
          if (m) {
            r.name = m[1];
            r.start = parse_date(m[2]);
            r.end = parse_date(m[3]);
            r.relation = m[5];
          } else {
            let m = l.match(/^(.+?) \((.+?)\)/);
            if (m) {
              r.name = m[1];
              r.relation = m[2];
            } else {
              r.name = l;
            }
          }
          if (r.relation in kinships) r.kinship = kinships[r.relation];
          if (r.relation == "divorced") r.cause = n_divorce;
          if (r.start == "?") r.start = undefined;
          if (r.end == "?" || r.end == "present") r.end = undefined;
          if (r.name == "None") continue;

          // Try to resolve name.
          r.relative = r.name;
          r.link = links.get(r.name);
          if (r.link) {
            let item = await context.idlookup(n_imdb, r.link);
            if (item) {
              r.item = item;
              r.relative = item;
              r.link = null;
              collector.add(item);
            }
          }

          relatives.push(r)
        }
      }
    }

    // Resolve gender-based kinship.
    await collector.retrieve();
    let parents = new Array();
    for (let r of relatives) {
      if (r.kinship.prop == n_parent) parents.push(r);
      if (r.kinship.female && r.kinship.male) {
        let gender;
        if (r.item) gender = r.item.get(n_gender);
        if (!gender) gender = await gender_for_name(r.name);
        if (gender == n_male) {
          r.kinship = kinships[r.kinship.male];
        } else if (gender == n_female) {
          r.kinship = kinships[r.kinship.female];
        }
      }
    }
    if (parents.length == 2) {
      if (parents[0].kinship.gender == n_male) {
        parents[1].kinship = kinships["mother"];
      } else if (parents[0].kinship.gender == n_female) {
        parents[1].kinship = kinships["father"];
      }
      if (parents[1].kinship.gender == n_male) {
        parents[0].kinship = kinships["mother"];
      } else if (parents[1].kinship.gender == n_female) {
        parents[0].kinship = kinships["father"];
      }
    }

    // Sort relatives according to rank and date.
    relatives.sort((r1, r2) => {
      if (r1.kinship.rank < r2.kinship.rank) return -1;
      if (r1.kinship.rank > r2.kinship.rank) return 1;
      let d1 = r1.start || r1.end;
      let d2 = r2.start || r2.end;
      if (d1 && d2) return d1 < d2 ? -1 : 1;
      if (d1) return -1;
      if (d2) return 1;
      return 0;
    });

    // Add family members to topic.
    for (let r of relatives) {
      if (topic.has(r.kinship.prop, r.relative)) continue;
      if (r.start || r.end || r.cause || r.link || r.kinship.qual) {
        let v = store.frame();
        v.add(store.is, r.relative);
        if (r.start) v.add(n_start_time, r.start);
        if (r.end) v.add(n_end_time, r.end);
        if (r.cause) v.add(n_end_cause, r.cause);
        if (r.kinship.qual) v.add(n_kinship, r.kinship.qual);
        if (r.link) v.add(n_imdb, r.link);
        topic.add(r.kinship.prop, v);
      } else {
        topic.add(r.kinship.prop, r.relative);
      }
    }

    topic.put(n_height, bio.height);


    // Add IMDB id.
    topic.put(n_imdb, imdbid);
    let item = await context.idlookup(n_imdb, imdbid);
    if (item) topic.put(store.is, item.id);

    // Add social links.
    await this.populate_social_links(context, topic, imdbid);

    context.updated(topic);
  }

  async populate_social_links(context, topic, imdbid) {
    // Get social links.
    let url = `https://www.imdb.com/name/${imdbid}/externalsites`;
    let r = await fetch(context.proxy(url));
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");

    // Add social links.
    let extsites = doc.getElementById("external_sites_content");
    if (extsites) {
      let sites = extsites.querySelectorAll("a.tracked-offsite-link");
      for (let link of sites.values()) {
        let href = link.getAttribute("href");
        if (href.includes(".blogspot.com")) continue;
        let [prop, identifier] = match_link(href);
        if (prop) {
          if (!topic.has(prop, identifier)) {
            topic.put(prop, identifier);
            let item = await context.idlookup(prop, identifier);
            if (item) topic.put(store.is, item.id);
          }
        }
      }
    }
  }
};


// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for biographies from imdb.com.

import {store, frame} from "/common/lib/global.js";
import {date_parser, ItemCollector} from "/common/lib/datatype.js";

import {SocialTopic} from "/case/app/social.js";
import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";

const n_name = frame("name");
const n_birth_name = frame("P1477");
const n_nickname = frame("P1449");
const n_instance_of = frame("P31");
const n_human = frame("Q5");
const n_date_of_birth = frame("P569");
const n_birthday = frame("P3150");
const n_place_of_birth = frame("P19");
const n_date_of_death = frame("P570");
const n_place_of_death = frame("P20");
const n_cause_of_death = frame("P509");
const n_country_of_citizenship = frame("P27");
const n_height = frame("P2048");
const n_cm = frame("Q174728");
const n_amount = frame("/w/amount");
const n_unit = frame("/w/unit");
const n_start_time = frame("P580");
const n_end_time = frame("P582");
const n_imdb = frame("P345");

const n_parent = frame("P8810");
const n_father = frame("P22");
const n_mother = frame("P25");
const n_sibling = frame("P3373");
const n_relative = frame("P1038");
const n_end_cause = frame("P1534");
const n_divorce = frame("Q93190");
const n_kinship = frame("P1039");
const n_num_children = frame("P1971");


const n_gender = frame("P21");
const n_female = frame("Q6581072");
const n_male = frame("Q6581097");

const kinships = {
  "Parents": {prop: n_parent, male: "Father", female: "Mother", rank: 13},
  "Father": {prop: n_father, gender: n_male, rank: 11},
  "Mother": {prop: n_mother, gender: n_female, rank: 12},

  "Sibling": {prop: n_sibling, rank: 20},
  "Half sibling": {prop: n_sibling, qual: frame("Q27965041"), rank: 21},

  "Spouse": {prop: frame("P26"), rank: 40},
  "Spouses": {prop: frame("P26"), rank: 40},
  "Children": {prop: frame("P40"), rank: 50},

  "Relatives": {prop: n_relative, rank: 1000},

  "Grandparent": {
    prop: n_relative,
    qual: frame("Q167918"),
    male: "grandfather",
    female: "grandmother",
    rank: 102,

  },
  "Grandfather": {prop: n_relative, qual: frame("Q9238344"), rank: 101},
  "Grandmother": {prop: n_relative, qual: frame("Q9235758"), rank: 102},

  "Great grandparent": {
    prop: n_relative,
    qual: frame("Q2500619"),
    male: "Great grandfather",
    female: "Great grandmother",
    rank: 106,
  },
  "Great grandfather": {
    prop: n_relative,
    qual: frame("Q2500621"),
    rank: 104,
  },
  "Great grandmother": {
    prop: n_relative,
    qual: frame("Q2500620"),
    rank: 105,
  },

  "Aunt or Uncle": {
    prop: n_relative,
    qual: frame("Q21073936"),
    male: "uncle",
    female: "aunt",
    rank: 113,
  },
  "Uncle": {prop: n_relative, qual: frame("Q76557"), rank: 111},
  "Aunt": {prop: n_relative, qual: frame("Q76507"), rank: 112},

  "Niece or Nephew": {
    prop: n_relative,
    qual: frame("Q76477"),
    male: "Nephew",
    female: "Niece",
    rank: 123,
  },
  "Nephew": {prop: n_relative, qual: frame("Q15224724"), rank: 121},
  "Niece": {prop: n_relative, qual: frame("Q3403377"), rank: 122},

  "Cousin": {prop: n_relative, qual: frame("Q23009870"), rank: 130},
  "Grandchild": {prop: n_relative, qual: frame("Q3603531"), rank: 140},
};

const gender_words = {
  "she": n_female,
  "her": n_female,
  "actress": n_female,
  "he": n_male,
  "his": n_male,
};

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

function full_date(d) {
  if (d < 10000) return d * 10000;
  if (d < 1000000) return d * 100;
  return d;
}

function parse_date(d) {
  if (!d) return undefined;
  let results = new Array();
  date_parser(d, results);
  if (results.length > 0) return results[0].value;
  return d;
}

async function parse_date_and_place(context, text) {
  let m = text.match(/^(.+)\s·\s(.*)\s+\(((.*))\)/);
  if (!m) m = text.match(/(.+)\s·\s(.*)/);
  if (!m) m = text.match(/(.+)/);

  let date = m[1].match(/^\w+ \d+$/) ? m[1] : parse_date(m[1]);

  let location, country;
  if (m[2]) {
    location = m[2].split(/, /);
    country = location.pop();
    location = await context.lookup(location.join(", "));
    country = await context.lookup(country);
  }

  let cause = m[3];
  if (cause == "undisclosed") cause = undefined;
  cause = await context.lookup(cause);

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
    let bio = {}
    let title = doc.querySelectorAll("h2")[0];
    bio.name = title && title.innerText.trim();

    // Find sections.
    let sections = new Map();
    let grid = doc.querySelector("div.ipc-page-grid");
    let section_list = grid.querySelectorAll("section.ipc-page-section");
    for (let section of section_list.values()) {
      let section_title = section.querySelector("div.ipc-title");
      if (!section_title) continue;
      let section_name = section_title.innerText;
      sections.set(section_name, section);
    }

    // Get basic information from overview table.
    let overview = sections.get("Overview");
    if (overview) {
      let list = overview.querySelector("ul");
      for (let row of list.querySelectorAll("li").values()) {
        let span = row.querySelector("span");
        let div = row.querySelector("div");
        if (!span || !div) continue;
        let label = span.innerText;
        let value = trim(div.innerText);

        if (label == "Born") {
          bio.birth = await parse_date_and_place(context, value);
        } else if (label == "Died") {
          bio.death = await parse_date_and_place(context, value);
        } else if (label == "Birth name") {
          bio.birthname = value;
        } else if (label == "Nickname") {
          bio.nickname = value;
        } else if (label == "Nicknames") {
          // Skip if multiple nicknames.
        } else if (label == "Height") {
          let m = value.match(/(\d)\.(\d+) m/);
          if (m) {
            let height = parseInt(m[2]);
            if (m[2].length == 1) height *= 10;
            height += parseInt(m[1]) * 100;
            bio.height = store.frame();
            bio.height.add(n_amount, height);
            bio.height.add(n_unit, n_cm);
          }
        } else {
          console.log("unknown overview item", label, value);
        }
      }
    }

    // Get gender from Self-verified table.
    let verified = sections.get("Self-verified on IMDbPro");
    if (verified) {
      let list = verified.querySelector("ul");
      for (let row of list.querySelectorAll("li").values()) {
        let span = row.querySelector("span");
        let div = row.querySelector("div");
        if (!span || !div) continue;
        let label = span.innerText;
        let value = trim(div.innerText);

        if (label == "Gender / Gender identity") {
          if (value == "Male") {
            bio.gender =  n_male;
          } else if (value == "Female") {
            bio.gender =  n_female;
          }
        }
      }
    }

    // Guess gender from bio text.
    if (!bio.gender) {
      let male_score = 0;
      let female_score = 0;
      for (let block of doc.querySelectorAll("div.ipc-html-content").values()) {
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
      if (male_score || female_score) {
        bio.gender =  male_score > female_score ? n_male : n_female;
      }
    }
    if (!bio.gender && bio.name) {
      bio.gender = await gender_for_name(bio.name);
    }

    // Add bio to topic.
    topic.put(n_name, bio.name);
    topic.put(n_birth_name, bio.birthname);
    topic.put(n_nickname, bio.nickname);
    topic.put(n_instance_of, n_human);
    topic.put(n_gender, bio.gender);
    if (bio.birth) {
      let bday = isNaN(bio.birth.date) ? n_birthday : n_date_of_birth;
      topic.put(bday, bio.birth.date);
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
    let family = sections.get("Family");
    if (family) {
      let list = family.querySelector("ul");
      for (let row of list.querySelectorAll("li").values()) {
        let span = row.querySelector("span");
        let div = row.querySelector("div");
        if (!span || !div) continue;

        // Get kinship from label.
        let label = span.innerText;
        let kinship = kinships[label];
        if (!kinship) {
          console.log("Unknown kinship: ", label);
          continue;
        }

        // Add family member(s).
        let list = div.querySelector("ul.ipc-inline-list");
        for (let item of list.children) {
          let line = item.innerText;
          let link;
          let a = item.querySelector("a");
          if (a) {
            let href = a.getAttribute("href");
            let m = href.match(/^\/name\/(nm\d+)/);
            link = m[1];
          }

          // Get name, period, and relation.
          let r = {kinship}
          let m = line.match(/^(.+?)\((.+?) - (.+?)\)( \((.+?)\))?/)
          if (m) {
            r.name = m[1];
            r.start = parse_date(m[2]);
            r.end = parse_date(m[3]);
            r.relation = m[5];
          } else {
            let m = line.match(/^(.+?)\((.+?)\)/);
            if (m) {
              r.name = m[1];
              r.relation = m[2];
            } else {
              r.name = line;
            }
          }

          if (r.relation) {
            for (let rel of r.relation.split(",")) {
              rel = rel.trim();
              if (rel in kinships) {
                r.kinship = kinships[r.relation];
              } else if (rel == "divorced") {
                r.cause = n_divorce;
              } else if (rel == "1 child") {
                r.children = 1;
              } else {
                let m = rel.match(/(\d+) children/);
                if (m) r.children = parseInt(m[1]);
              }
            }
          }
          if (r.start == "?") r.start = undefined;
          if (r.end == "?" || r.end == "present") r.end = undefined;
          if (r.name == "None") continue;

          // Try to resolve name.
          r.relative = r.name;
          r,link = link;
          if (link) {
            let item = await context.idlookup(n_imdb, link);
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
        parents[1].kinship = kinships["Mother"];
      } else if (parents[0].kinship.gender == n_female) {
        parents[1].kinship = kinships["Father"];
      }
      if (parents[1].kinship.gender == n_male) {
        parents[0].kinship = kinships["Mother"];
      } else if (parents[1].kinship.gender == n_female) {
        parents[0].kinship = kinships["Father"];
      }
    }

    // Sort relatives according to rank and date.
    relatives.sort((r1, r2) => {
      if (r1.kinship.rank < r2.kinship.rank) return -1;
      if (r1.kinship.rank > r2.kinship.rank) return 1;
      let d1 = r1.start || r1.end;
      let d2 = r2.start || r2.end;
      if (d1 && d2) return full_date(d1) < full_date(d2) ? -1 : 1;
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
        if (r.children) v.add(n_num_children, r.children);
        if (r.link) v.add(n_imdb, r.link);
        topic.add(r.kinship.prop, v);
      } else {
        topic.add(r.kinship.prop, r.relative);
      }
    }

    if (!topic.has(n_height)) topic.put(n_height, bio.height);

    // Add social links.
    await this.populate_social_links(context, topic, imdbid);

    context.updated(topic);
  }

  async populate_social_links(context, topic, imdbid) {
    // Add IMDB id.
    let social = new SocialTopic(topic, context);
    await social.add_prop(n_imdb, imdbid);

    // Get social links.
    let url = `https://www.imdb.com/name/${imdbid}/externalsites`;
    let r = await fetch(context.proxy(url));
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");

    // Add social links.
    let sites = doc.querySelectorAll("a.ipc-metadata-list-item__label");
    for (let link of sites.values()) {
      let href = link.getAttribute("href");
      if (href.includes(".blogspot.com")) continue;
      await social.add_link(href);
    }
  }
}


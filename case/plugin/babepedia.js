// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from babepedia.com.

import {store} from "/case/app/global.js";
import {SocialTopic, strip_emojis} from "/case/app/social.js";

const n_name = store.lookup("name");
const n_alias = store.lookup("alias");
const n_media = store.lookup("media");
const n_instance_of = store.lookup("P31");
const n_human = store.lookup("Q5");
const n_gender = store.lookup("P21");
const n_date_of_birth = store.lookup("P569");
const n_place_of_birth = store.lookup("P19");
const n_date_of_death = store.lookup("P570");
const n_country_of_citizenship = store.lookup("P27");
const n_occupation = store.lookup("P106");
const n_height = store.lookup("P2048");
const n_weight = store.lookup("P2067");
const n_hair_color = store.lookup("P1884");
const n_eye_color = store.lookup("P1340");
const n_work_peroid_start = store.lookup("P2031");
const n_work_peroid_end = store.lookup("P2032");
const n_described_by_url = store.lookup("P973");
const n_female = store.lookup("Q6581072");
const n_cm = store.lookup("Q174728");
const n_kg = store.lookup("Q11570");
const n_amount = store.lookup("/w/amount");
const n_unit = store.lookup("/w/unit");

const hair_colors = {
  "brown": store.lookup("Q2367101"),
  "blonde": store.lookup("Q202466"),
  "black": store.lookup("Q1922956"),
  "auburn": store.lookup("Q2419551"),
  "red": store.lookup("Q152357"),
};

const eye_colors = {
  "brown": store.lookup("Q17122705"),
  "blue": store.lookup("Q17122834"),
  "green": store.lookup("Q17122854"),
  "hazel": store.lookup("Q17122740"),
  "grey": store.lookup("Q17245659"),
};

const occupations = {
  "centerfold": store.lookup("Q3286043"),
  "playboy model": store.lookup("Q728711"),
  "actress": store.lookup("Q33999"),
  "milf porn star": store.lookup("Q488111"),
}

function date2sling(d) {
  return d.getFullYear() * 10000 + (d.getMonth() + 1) * 100 + d.getDate();
}

async function lookup(context, name) {
  if (name.length == 0) return null;
  let r = await context.kblookup(name, {fullmatch: 1});
  let data = await r.json();
  if (data.matches.length > 0) {
    return store.lookup(data.matches[0].ref);
  } else {
    return name;
  }
}

export default class BabepediaPlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let m = url.pathname.match(/^\/babe\/([A-Za-z0-9\._\-]+)/);
    if (!m) return;
    let username = m[1];
    if (!username) return;

    if (action == 1) { // SEARCHURL
      console.log("babepedia search for", username);
      return {
        ref: username,
        name: username.replace(/_/g, " "),
        description: "Babepedia model",
        url: url.href,
        context: context,
        onitem: item => this.select(item),
      };
    } else if (action == 3) { // PASTEURL
      await this.populate(context, context.topic, url.href);
      return true;
    }
  }

  async select(item) {
    // Create new topic.
    let topic = item.context.new_topic();
    if (!topic) return;

    // Fetch profile from babepedia and populate topic.
    await this.populate(item.context, topic, item.url);

    // Update topic list.
    await item.context.editor.update_topics();
    await item.context.editor.navigate_to(topic);
  }

  async populate(context, topic, url) {
    // Retrieve babepedia profile for user.
    let r = await fetch(context.proxy(url));
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");

    // Get name and aliases.
    let babename = doc.getElementById("babename");
    if (babename) {
      let name = babename.innerText;
      topic.put(n_name, name);
    }
    let aka = doc.getElementById("aka");
    if (aka) {
      let names = aka.innerText.substring(4).split(/\s*\/\s*/);
      for (let name of names) {
        topic.put(n_alias, name);
      }
    }
    topic.put(n_instance_of, n_human);
    topic.put(n_gender, n_female);

    // Get biographical information.
    let biolist = doc.getElementById("biolist");
    for (let i = 0; i < biolist.children.length; i++) {
      let li = biolist.children[i];
      let m = li.innerText.match(/^([^:]+): ?(.+)/);
      let field = m[1];
      let value = m[2];
      if (field == "Born") {
        let m = value.match(/\w+ (\d+)\w+ of (\w+) (\d+)/);
        let date = new Date(m[1] + " " + m[2] + " " + m[3]);
        topic.put(n_date_of_birth, date2sling(date));
      } else if (field == "Died") {
        let m = value.match(/\w+ (\d+)\w+ of (\w+) (\d+)/);
        let date = new Date(m[1] + " " + m[2] + " " + m[3]);
        topic.put(n_date_of_death, date2sling(date));
      } else if (field == "Birthplace") {
        let location = value.split(/, /);
        let country = location.pop();
        location = await lookup(context, location.join(", "));
        country = await lookup(context, country);
        if (location) {
          topic.put(n_place_of_birth, location);
          topic.put(n_country_of_citizenship, country);
        } else if (country) {
          topic.put(n_place_of_birth, country);
        }
      } else if (field == "Profession") {
        for (let profession of value.split(/, /)) {
          let occ = profession.toLowerCase();
          if (occ.endsWith(" (former)")) {
            occ = occ.substring(0, occ.length - 9);
          }
          if (occ in occupations) {
            occ = occupations[occ];
          } else {
            occ = await lookup(context, occ);
          }
          if (occ && !topic.has(n_occupation, occ)) {
            topic.put(n_occupation, occ);
          }
        }
      } else if (field == "Height") {
        let m = value.match(/\(or (\d+) cm\)/);
        if (m) {
          let v = store.frame();
          v.add(n_amount, parseInt(m[1]));
          v.add(n_unit, n_cm);
          topic.put(n_height, v);
        }
      } else if (field == "Weight") {
        let m = value.match(/\(or (\d+) kg\)/);
        if (m) {
          let v = store.frame();
          v.add(n_amount, parseInt(m[1]));
          v.add(n_unit, n_kg);
          topic.put(n_weight, v);
        }
      } else if (field == "Hair color") {
        let color = value.toLowerCase();
        if (color in hair_colors) color = hair_colors[color];
        if (color) {
          topic.put(n_hair_color, color);
        }
      } else if (field == "Eye color") {
        let color = value.toLowerCase();
        if (color in eye_colors) color = eye_colors[color];
        if (color) {
          topic.put(n_eye_color, color);
        }
      } else if (field == "Years active") {
        let m = value.match(/(\d+) - (\d+|present)/);
        if (m) {
          topic.put(n_work_peroid_start, parseInt(m[1]));
          if (m[2] != "present") {
            topic.put(n_work_peroid_end, parseInt(m[2]));
          }
        }
      } else {
        console.log("field", field, "value", value);
      }
    }

    // Add social links.
    let socialicons = doc.getElementById("socialicons");
    if (socialicons) {
      let social = new SocialTopic(topic, context);
      for (let i = 0; i < socialicons.children.length; i++) {
        let a = socialicons.children[i];
        let url = a.getAttribute("href");
        await social.add_link(url);
      }
    }

    // Add reference to babepedia.
    topic.put(n_described_by_url, url);

    // Add profile photo.
    let profimg = doc.getElementById("profimg");
    if (profimg) {
      let a = profimg.querySelector("a");
      if (a) {
        let href = a.getAttribute("href");
        if (!href.startsWith("javascript")) {
          let image = "https://babepedia.com" + a.getAttribute("href");
          topic.put(n_media, image);
        }
      }
    }
  }
};


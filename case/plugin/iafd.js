// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from iafd.com.

import {store, frame} from "/common/lib/global.js";

import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";

const n_name = frame("name");
const n_alias = frame("alias");
const n_media = frame("media");
const n_instance_of = frame("P31");
const n_human = frame("Q5");
const n_gender = frame("P21");
const n_date_of_birth = frame("P569");
const n_place_of_birth = frame("P19");
const n_date_of_death = frame("P570");
const n_country_of_citizenship = frame("P27");
const n_occupation = frame("P106");
const n_pornstar = frame("Q488111");
const n_height = frame("P2048");
const n_weight = frame("P2067");
const n_hair_color = frame("P1884");
const n_work_peroid_start = frame("P2031");
const n_work_peroid_end = frame("P2032");
const n_iafd = frame("P3869");
const n_female = frame("Q6581072");
const n_cm = frame("Q174728");
const n_kg = frame("Q11570");
const n_amount = frame("/w/amount");
const n_unit = frame("/w/unit");
const n_usa = frame("Q30");

const hair_colors = {
  "Brown": frame("Q2367101"),
  "Blonde": frame("Q202466"),
  "Blond": frame("Q202466"),
  "Black": frame("Q1922956"),
  "Auburn": frame("Q2419551"),
  "Red": frame("Q152357"),
};

function date2sling(d) {
  if (d.getMonth() == 0 && d.getDate() == 1) {
    return d.getFullYear();
  } else {
    return d.getFullYear() * 10000 + (d.getMonth() + 1) * 100 + d.getDate();
  }
}

export default class IAFDPlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let m = url.pathname.match(/^\/person.rme\/perfid=([^\/]+)/);
    if (!m) return;
    let username = decodeURIComponent(m[1]);
    if (!username) return;

    if (action == SEARCHURL) {
      return {
        ref: username,
        name: username.replace(/_/g, " "),
        description: "IAFD model",
        url: url.href,
        context: context,
        onitem: item => this.select(item),
      };
    } else if (action == PASTEURL) {
      await this.populate(context, context.topic, url.href, username);
      return true;
    }
  }

  async select(item) {
    // Create new topic.
    let topic = await item.context.new_topic();
    if (!topic) return;

    // Fetch profile from IAFD and populate topic.
    await this.populate(item.context, topic, item.url, item.ref);
  }

  async populate(context, topic, url, ref) {
    // Retrieve babepedia profile for user.
    let r = await context.fetch(url);
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");

    // Get title.
    let bio = {};
    bio.title = doc.querySelector("h1").innerText.trim();

    // Get photo.
    let headshot = doc.querySelector("div#headshot img");
    if (headshot) bio.photo = headshot.getAttribute("src");

    // Get bio fields.
    let fields = new Map();
    for (let heading of doc.querySelectorAll("p.bioheading").values()) {
      let data = heading.nextSibling;
      let label = heading.innerText.replace(/\n/g, " ").trim();
      let value = data.innerText.trim();
      if (value == "No data") continue;
      fields.set(label, value);
    }

    // Aliases.
    bio.akas = [];
    let akas = fields.get("Performer AKA");
    if (akas && akas != "No known aliases") {
      for (let aka of akas.split(",")) {
        bio.akas.push(aka.trim());
      }
    }

    // Birth day.
    let birth = fields.get("Birthday");
    if (birth) {
      let paren = birth.indexOf('(');
      if (paren != -1) birth = birth.slice(0, paren).trim();
      bio.born = date2sling(new Date(birth));
    }

    // Death.
    let death = fields.get("Date of Death");
    if (death) {
      let paren = death.indexOf('(');
      if (paren != -1) death = death.slice(0, paren).trim();
      bio.died = date2sling(new Date(death));
    }

    // Birth place.
    let bplace = fields.get("Birthplace");
    if (bplace) {
      if (bplace.endsWith(", USA")) {
        bplace = bplace.slice(0, -5);
        bio.country = n_usa;
      }
      bio.birthplace = await context.lookup(bplace);
    }
    let nationality = fields.get("Nationality");
    if (nationality) {
      bio.country = await context.lookup(nationality);
    }

    // Work period.
    let active = fields.get("Years Active");
    if (active) {
      let m = active.match(/^(\d+)-(\d+)/) || active.match(/^(\d+)/);
      if (m) {
        bio.start = date2sling(new Date(m[1]));
        bio.end = date2sling(new Date(m[2] || m[1]));
      }
    }
    active = fields.get("Year Active");
    if (active) {
      let year = parseInt(active);
      if (!isNaN(year)) {
        bio.start = year;
        bio.end = year;
      }
    }

    // Heigth, weight, and hair color.
    let height = fields.get("Height");
    if (height) {
      let m = height.match(/\((\d+) cm\)/);
      if (m) bio.height = parseInt(m[1]);
    }
    let weight = fields.get("Weight");
    if (weight) {
      let m = weight.match(/\((\d+) kg\)/);
      if (m) bio.weight = parseInt(m[1]);
    }
    let hair = fields.get("Hair Color");
    if (hair) {
      bio.hair = hair_colors[hair] || hair;
    }

    // Update topic with bio.
    if (!topic) topic = await context.new_topic();
    topic.put(n_name, bio.title);
    for (let aka of bio.akas) {
      topic.put(n_alias, aka);
    }
    topic.put(n_instance_of, n_human);
    topic.put(n_gender, n_female);
    topic.put(n_date_of_birth, bio.born);
    topic.put(n_place_of_birth, bio.birthplace);
    topic.put(n_date_of_death, bio.died);
    topic.put(n_country_of_citizenship, bio.country);
    topic.put(n_occupation, n_pornstar);
    topic.put(n_work_peroid_start, bio.start);
    topic.put(n_work_peroid_end, bio.end);
    if (bio.height && !topic.has(n_height)) {
      let v = store.frame();
      v.add(n_amount, bio.height);
      v.add(n_unit, n_cm);
      topic.add(n_height, v);
    }
    if (bio.weight && !topic.has(n_weight)) {
      let v = store.frame();
      v.add(n_amount, bio.weight);
      v.add(n_unit, n_kg);
      topic.add(n_weight, v);
    }
    topic.put(n_hair_color, bio.hair);
    topic.put(n_iafd, ref);
    if (bio.photo && !bio.photo.includes("nophoto")) {
      topic.put(n_media, bio.photo);
    }
    context.updated(topic);
  }
};


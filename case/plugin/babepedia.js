// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from babepedia.com.

import {store, frame} from "/common/lib/global.js";

import {SocialTopic, strip_emojis} from "/case/app/social.js";
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
const n_height = frame("P2048");
const n_weight = frame("P2067");
const n_hair_color = frame("P1884");
const n_eye_color = frame("P1340");
const n_work_peroid_start = frame("P2031");
const n_work_peroid_end = frame("P2032");
const n_babepedia_id = frame("PBABE");
const n_female = frame("Q6581072");
const n_cm = frame("Q174728");
const n_kg = frame("Q11570");
const n_amount = frame("/w/amount");
const n_unit = frame("/w/unit");

const hair_colors = {
  "brown": frame("Q2367101"),
  "blonde": frame("Q202466"),
  "black": frame("Q1922956"),
  "auburn": frame("Q2419551"),
  "red": frame("Q152357"),
  "redhead": frame("Q152357"),
};

const eye_colors = {
  "brown": frame("Q17122705"),
  "blue": frame("Q17122834"),
  "green": frame("Q17122854"),
  "hazel": frame("Q17122740"),
  "grey": frame("Q17245659"),
};

const occupations = {
  "centerfold": frame("Q3286043"),
  "playboy model": frame("Q728711"),
  "actress": frame("Q33999"),
  "milf porn star": frame("Q488111"),
  "tiktok star": frame("Q94791573"),
  "escort": frame("Q814356"),
  "glamour model": frame("Q3286043"),
  "gamer": frame("Q5276395"),
}

function date2sling(d) {
  if (d.getMonth() == 0 && d.getDate() == 1) {
    return d.getFullYear();
  } else {
    return d.getFullYear() * 10000 + (d.getMonth() + 1) * 100 + d.getDate();
  }
}

export default class BabepediaPlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let m = url.pathname.match(/^\/babe\/([^\/]+)/);
    if (!m) return;
    let username = decodeURIComponent(m[1]);
    if (!username) return;

    if (action == SEARCHURL) {
      console.log("babepedia search for", username);
      return {
        ref: username,
        name: username.replace(/_/g, " "),
        description: "Babepedia model",
        url: url.href,
        context: context,
        onitem: item => this.select(item),
      };
    } else if (action == PASTEURL) {
      await this.populate(context, context.topic, url.href);
      return true;
    }
  }

  async select(item) {
    // Create new topic.
    let topic = await item.context.new_topic();
    if (!topic) return;

    // Fetch profile from babepedia and populate topic.
    await this.populate(item.context, topic, item.url);
  }

  async populate(context, topic, url) {
    // Retrieve babepedia profile for user.
    let r = await context.fetch(url);
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");

    // Create new topic if missing.
    if (!topic) topic = await context.new_topic();

    // Get name and aliases.
    let babename = doc.getElementById("babename");
    if (babename) {
      let name = babename.innerText.trim();
      topic.put(n_name, name);
    }
    let aka = doc.getElementById("aka");
    if (aka) {
      let names = aka.innerText.substring(15).split(/\s-\s*/);
      for (let name of names) {
        topic.put(n_alias, name.trim());
      }
    }
    topic.put(n_instance_of, n_human);
    topic.put(n_gender, n_female);

    // Get biographical information.
    let   bio = {};
    bio.occ = [];
    let biolist = doc.querySelector("div.info-grid");
    if (biolist) {
      for (let i = 0; i < biolist.children.length; i++) {
        let li = biolist.children[i];
        if (li.className != "info-item") continue;
        let field = li.firstElementChild.innerText.trim();
        let value = li.lastElementChild.innerText.trim();
        if (field == "Born:") {
          let m = value.match(/\w+ (\d+)\w+ of (\w+) (\d+)/);
            let date = new Date(m ? m[1] + " " + m[2] + " " + m[3] : value);
            bio.dob = date2sling(date);
        } else if (field == "Died:") {
          let m = value.match(/\w+ (\d+)\w+ of (\w+) (\d+)/);
          let date = new Date(m ? m[1] + " " + m[2] + " " + m[3] : value);
          bio.dod = date2sling(date);
        } else if (field == "Birthplace:") {
          let location = value.split(/, /);
          let country = location.pop();
          if (country == "Republic of") {
            country = "Republic of " + location.pop();
          }
          location = await context.lookup(location.join(", "));
          country = await context.lookup(country);
          if (location) {
            bio.pob = location
            bio.country = country;
          } else if (country) {
            bio.pob = country;
          }
        } else if (field == "Professions:") {
          for (let profession of value.split(/, /)) {
            let occ = profession.toLowerCase();
            if (occ.startsWith("sportswoman: ")) {
              occ = occ.substring(13);
            }
            if (occ.endsWith(" (former)")) {
              occ = occ.substring(0, occ.length - 9);
            }
            if (occ in occupations) {
              occ = occupations[occ];
            } else {
              occ = await context.lookup(occ);
            }
            if (occ && !bio.occ.includes(occ)) {
              bio.occ.push(occ);
            }
          }
        } else if (field == "Height:") {
          let m = value.match(/\(or (\d+) cm\)/);
          if (m) {
            let v = store.frame();
            v.add(n_amount, parseInt(m[1]));
            v.add(n_unit, n_cm);
            bio.height = v;
          }
        } else if (field == "Weight:") {
          let m = value.match(/\(or (\d+) kg\)/);
          if (m) {
            let v = store.frame();
            v.add(n_amount, parseInt(m[1]));
            v.add(n_unit, n_kg);
            bio.weight = v;
          }
        } else if (field == "Hair color:") {
          let color = value.toLowerCase();
          if (color in hair_colors) color = hair_colors[color];
          if (color) bio.hair = color
        } else if (field == "Eye color:") {
          let color = value.toLowerCase();
          if (color in eye_colors) color = eye_colors[color];
          bio.eye = color;
        } else if (field == "Years active:") {
          let m = value.match(/(\d+)\s*-\s*(\d+|present)/);
          if (m) {
            bio.start = parseInt(m[1]);
            if (m[2] != "present") bio.end = parseInt(m[2]);
          }
        }
      }
    }


    topic.put(n_date_of_birth, bio.dob);
    topic.put(n_place_of_birth, bio.pob);
    topic.put(n_date_of_death, bio.dod);
    topic.put(n_country_of_citizenship, bio.country);
    for (let occ of bio.occ) topic.put(n_occupation, occ);
    topic.put(n_work_peroid_start, bio.start);
    topic.put(n_work_peroid_end, bio.end);
    if (!topic.has(n_height)) topic.put(n_height, bio.height);
    if (!topic.has(n_weight)) topic.put(n_weight, bio.weight);
    topic.put(n_hair_color, bio.hair);
    topic.put(n_eye_color, bio.eyes);

    // Add social links.
    let social = new SocialTopic(topic, context);
    let socialicons = doc.getElementById("socialicons");
    if (socialicons) {
      for (let i = 0; i < socialicons.children.length; i++) {
        let a = socialicons.children[i];
        let url = a.getAttribute("href");
        await social.add_link(url);
      }
    }
    let linkstable = doc.querySelector("div.linkstable");
    if (linkstable) {
      for (let link of linkstable.querySelectorAll("a.outlink").values()) {
        let url = link.getAttribute("href");
        await social.add_link(url);
      }
    }

    // Add reference to babepedia.
    let m = new URL(url).pathname.match(/^\/babe\/([^\/]+)/);
    let username = decodeURIComponent(m[1]);
    await social.add_prop(n_babepedia_id, username);

    // Add images.
    let imgurls = new Array();
    let profimg = doc.getElementById("profimg");
    if (profimg) {
      let a = profimg.querySelector("a.img");
      if (a) imgurls.push(a.getAttribute("href"));
    }

    for (let prof of doc.querySelectorAll("div.prof").values()) {
      let a = prof.querySelector("a.img");
      if (a) imgurls.push(a.getAttribute("href"));
    }


    for (let prof of doc.querySelectorAll("div.thumbnail").values()) {
      let a = prof.querySelector("a.img");
      if (a) imgurls.push(a.getAttribute("href"));
    }

    for (let imgurl of imgurls) {
      if (imgurl.startsWith("/")) imgurl = imgurl.slice(1);
      topic.put(n_media, "!https://www.babepedia.com/" + imgurl);
    }

    context.updated(topic);
  }
};

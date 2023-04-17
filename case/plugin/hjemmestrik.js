// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from hjemmestrik.dk.

import {Frame} from "/common/lib/frame.js";
import {store, frame} from "/common/lib/global.js";

import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";

const n_is = frame("is");
const n_name = frame("name");
const n_media = frame("media");
const n_instance_of = frame("P31");
const n_human = frame("Q5");
const n_gender = frame("P21");
const n_date_of_birth = frame("P569");
const n_residence = frame("P551");
const n_country = frame("P27");
const n_occupation = frame("P106");
const n_height = frame("P2048");
const n_weight = frame("P2067");
const n_described_by_url = frame("P973");
const n_female = frame("Q6581072");
const n_denmark = frame("Q35");
const n_cm = frame("Q174728");
const n_kg = frame("Q11570");
const n_amount = frame("/w/amount");
const n_unit = frame("/w/unit");
const n_award_received = frame("P166");
const n_point_in_time = frame("P585");
const n_sh_girl = frame("t/1052/1");

// First day of week in year.
function date_of_week(y, w) {
  var d = new Date(y, 0, 1 + (w - 1) * 7);
  var dow = d.getDay();
  var start = d;
  if (dow <= 4) {
    start.setDate(d.getDate() - d.getDay() + 1);
  } else {
    start.setDate(d.getDate() + 8 - d.getDay());
  }
  return start;
}

// Se & Hør is published weekly on Wednesdays.
function sh_date(y, w) {
  var date = date_of_week(y, w);
  date.setDate(date.getDate() + 2);
  return date;
}

// Date as SLING numeric date.
function date_number(d) {
  let year = d.getFullYear();
  let month = d.getMonth() + 1;
  let day = d.getDate();
  return year * 10000 + month * 100 + day;
}

class Sign {
  constructor(begin_month, begin_day, end_month, end_day) {
    this.begin_month = begin_month;
    this.begin_day = begin_day;
    this.end_month = end_month;
    this.end_day = end_day;
  }

  compare(month, day) {
    if (month < this.begin_month || day < this.begin_day) return -1;
    if (month > this.end_month || day > this.end_day) return 1;
    return 0;
  }
};

const aries = new Sign(3, 21, 4, 19)
const taurus = new Sign(4, 20, 5, 21)
const gemini = new Sign(5, 22, 6, 21)
const cancer = new Sign(6, 22, 7, 22)
const leo = new Sign(7, 23, 8, 22)
const virgo = new Sign(8, 23, 9, 22)
const libra = new Sign(9, 23, 10, 22)
const scorpio = new Sign(10, 23, 11, 22)
const sagittarius = new Sign(11, 23, 12, 21)
const capricorn = new Sign(12, 22, 1, 20)
const aquarius = new Sign(1, 21, 2, 18)
const pisces = new Sign(2, 19, 3, 20)

const zodiac = {
  "aries": aries,
  "taurus": taurus,
  "gemini": gemini,
  "cancer": cancer,
  "leo": leo,
  "virgo": virgo,
  "libra": libra,
  "scorpio": scorpio,
  "sagittarius": sagittarius,
  "capricorn": capricorn,
  "aquarius": aquarius,
  "pisces": pisces,

  "vædderen": aries,
  "vædder": aries,
  "tyren": taurus,
  "tyr": taurus,
  "tvillingerne": gemini,
  "tvillingen": gemini,
  "tvilling": gemini,
  "krebsen": cancer,
  "krebs": cancer,
  "løven": leo,
  "løve": leo,
  "jomfruen": virgo,
  "jomfrue": virgo,
  "vægten": libra,
  "vægt": libra,
  "skorpionen": scorpio,
  "skorpion": scorpio,
  "skytten": sagittarius,
  "skytte": sagittarius,
  "stenbukken": capricorn,
  "stenbuk": capricorn,
  "vandmanden": aquarius,
  "vandmand": aquarius,
  "fiskene": pisces,
  "fisken": pisces,
  "fisk": pisces,
};

async function lookup(context, name) {
  if (name.length == 0) return null;
  let r = await context.kblookup(name, {fullmatch: 1});
  let data = await r.json();
  if (data.matches.length > 0) {
    return frame(data.matches[0].ref);
  } else {
    return name;
  }
}

export default class HjemmestrikPlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let m = url.pathname.match(/^\/pige\/\d+\/\d+\/(.+)/);
    if (!m) return;
    let model = decodeURIComponent(m[1]);
    if (!model) return;

    if (action == SEARCHURL) {
      return {
        ref: url,
        name: model,
        description: "Se og Hør model",
        context: context,
        onitem: item => this.select(item),
      };
    } else if (action == PASTEURL) {
      await this.populate(context, context.topic, url);
      return true;
    }
  }

  async select(item) {
    // Create new topic.
    let topic = await item.context.new_topic();
    if (!topic) return;

    // Fetch profile from hjemmestrik and populate topic.
    await this.populate(item.context, topic, item.ref);
  }

  async populate(context, topic, url) {
    // Retrieve hjemmestrik profile for user.
    let r = await fetch(context.proxy(url), {headers: {
      "XUser-Agent": navigator.userAgent,
    }});
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");

    // Get profile.
    let profile = doc.getElementById("profile");
    let props = new Map();
    let name = null;
    let shlinks = new Array();
    let hslinks = new Array();
    for (let i = 0; i < profile.children.length; i++) {
      let e = profile.children[i];
      let type = e.tagName;
      let text = e.innerText;

      if (type == "H2") {
        name = text;
      } else if (type == "P") {
        let m = text.match(/([A-ZÆØÅa-zæøå ]+)[:\?] ?(.+)/);
        if (m) {
          let field = m[1];
          let value = m[2];
          props[field] = value;
        }
      } else if (type == "A") {
        let href = e.getAttribute("href");
        let m = href.match(/^https:\/\/hjemmestrik\.dk\/pige\//);
        if (m) {
          hslinks.push(href);
        } else {
          shlinks.push(href);
        }
      }
    }

    // Add biographical information.
    topic.put(n_name, name);
    topic.put(n_instance_of, n_human);
    topic.put(n_gender, n_female);

    // Get week and year of award.
    let time = props["Uge"].match(/(\d+) \/ år: (\d+)/);
    let week = parseInt(time[1]);
    let year = parseInt(time[2]);
    let date = sh_date(year, week);

    // Get age and zodiac sign and compute birth year.
    if (props["Alder"]) {
      let alder = props["Alder"].match(/(\d+) år/);
      let age = parseInt(alder[1]);
      let born = year - age;
      if (props["Stjernetegn"]) {
        let sign = zodiac[props["Stjernetegn"].toLowerCase()];
        if (sign) {
          if (sign.compare(date.getMonth() + 1, date.getdate) < 0) born -= 1;
        }
      }
      topic.put(n_date_of_birth, born);
    }

    // Add residence.
    let city = props["By"];
    if (city) {
      let location = await lookup(context, city);
      topic.put(n_residence, location);
    }
    topic.put(n_country, n_denmark);

    // Add occupation.
    let job = props["Beskæftigelse"];
    if (job) {
      let occupation = await lookup(context, job);
      topic.put(n_occupation, occupation);
    }

    // Add height and weight.
    if (props["Højde"] && !topic.has(n_height)) {
      let m = props["Højde"].match(/(\d+) cm\.?/);
      if (m) {
        let v = store.frame();
        v.add(n_amount, parseInt(m[1]));
        v.add(n_unit, n_cm);
        topic.put(n_height, v);
      }
    }
    if (props["Vægt"] && !topic.has(n_weight)) {
      let m = props["Vægt"].match(/(\d+) kg\.?/);
      if (m) {
        let v = store.frame();
        v.add(n_amount, parseInt(m[1]));
        v.add(n_unit, n_kg);
        topic.put(n_weight, v);
      }
    }

    // Recursively add links for same model.
    for (let n = 0; n < hslinks.length; ++n) {
      let r = await fetch(context.proxy(hslinks[n]), {headers: {
        "XUser-Agent": navigator.userAgent,
      }});
      let html = await r.text();
      let doc = new DOMParser().parseFromString(html, "text/html");
      let profile = doc.getElementById("profile");
      for (let i = 0; i < profile.children.length; i++) {
        let e = profile.children[i];
        let type = e.tagName;
        let text = e.innerText;
        if (type == "A") {
          let href = e.getAttribute("href");
          let m = href.match(/^https:\/\/hjemmestrik\.dk\/pige\//);
          if (m) {
            if (!hslinks.includes(href)) hslinks.push(href);
          } else {
            if (!shlinks.includes(href)) shlinks.push(href);
          }
        }
      }
    }

    // Add award(s).
    let seen = new Set();
    for (let award of topic.all(n_award_received)) {
      console.log(typeof(award));
      if (award instanceof Frame) {
        let dt = award.get(n_point_in_time);
        if (dt) seen.add(dt);
      }
    }
    if (!seen.has(date_number(date))) {
      let award = store.frame();
      award.add(n_is, n_sh_girl);
      award.add(n_point_in_time, date_number(date));
      topic.put(n_award_received, award);
      seen.add(date_number(date));
    }
    for (let link of hslinks) {
      let m = link.match(/^https:\/\/hjemmestrik\.dk\/pige\/(\d+)\/(\d+)\//);
      if (m) {
        let year = parseInt(m[2]);
        let week = parseInt(m[1]);
        let date = sh_date(year, week);
        let award = store.frame();
        let dt = date_number(date);
        if (seen.has(dt)) continue;
        seen.add(dt);
        award.add(n_is, n_sh_girl);
        award.add(n_point_in_time, dt);
        topic.put(n_award_received, award);
      }
    }

    // Add links.
    let shurl = null;
    for (let link of shlinks) {
      if (link.match(/https?:\/\/www\.seoghoer\.dk\//)) {
        shurl = decodeURI(link);
        topic.put(n_described_by_url, shurl);
      }
    }

    // Get profile picture(s).
    for (let shurl of shlinks) {
      let r = await fetch(context.proxy(shurl));
      let html = await r.text();
      let doc = new DOMParser().parseFromString(html, "text/html");

      let content = doc.querySelector("div.content");
      if (content) {
        let pictures = content.querySelectorAll("img[srcset]");
        for (let i = 0; i < pictures.length; ++i) {
          let img = pictures[i];
          let src = img.getAttribute("src");
          let qpos = src.indexOf("?");
          if (qpos > 0) src = src.substr(0, qpos);
          topic.put(n_media, "!" + src);
        }
      } else {
        let primary = doc.querySelector("div.primary-media");
        let pictures = (primary || doc).querySelectorAll("picture");
        for (let i = 0; i < pictures.length; ++i) {
          let img = pictures[i].querySelector("img.img__thumbnail");
          if (!img) continue;
          let src = img.getAttribute("src");
          let qpos = src.indexOf("?");
          if (qpos > 0) src = src.substr(0, qpos);
          topic.put(n_media, "!" + src);
        }
      }
    }

    context.updated(topic);
  }
};


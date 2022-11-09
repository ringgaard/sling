// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from listal.com.

import {store} from "/case/app/global.js";
import {SocialTopic} from "/case/app/social.js";
import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";

const n_name = store.lookup("name");
const n_birth_name = store.lookup("P1477");
const n_instance_of = store.lookup("P31");
const n_human = store.lookup("Q5");
const n_date_of_birth = store.lookup("P569");
const n_date_of_death = store.lookup("P570");
const n_place_of_birth = store.lookup("P19");
const n_country_of_citizenship = store.lookup("P27");
const n_residence = store.lookup("P551");
const n_gender = store.lookup("P21");
const n_female = store.lookup("Q6581072");
const n_male = store.lookup("Q6581097");
const n_listal = store.lookup("PLSTL");
const n_height = store.lookup("P2048");
const n_amount = store.lookup("/w/amount");
const n_unit = store.lookup("/w/unit");
const n_cm = store.lookup("Q174728");
const n_spouse = store.lookup("P26");
const n_partner = store.lookup("P451");
const n_media = store.lookup("media");
const n_occupation = store.lookup("P106");
const n_hair_color = store.lookup("P1884");
const n_eye_color = store.lookup("P1340");
const n_manner_of_death = store.lookup("P1196");
const n_sexual_orientation = store.lookup("P91");

function stmt(prop, value) {
  return [prop, store.lookup(value)];
}

const tagsdir = {
  "Model": stmt(n_occupation, "Q4610556"),
  "Actress": stmt(n_occupation, "Q33999"),
  "Actor": stmt(n_occupation, "Q33999"),
  "Tv Actress": stmt(n_occupation, "Q10798782"),
  "Television Actress": stmt(n_occupation, "Q10798782"),
  "Film Actress": stmt(n_occupation, "Q10800557"),
  "Fashion Model": stmt(n_occupation, "Q3357567"),
  "Supermodel": stmt(n_occupation, "Q865851"),
  "Social Media Star": stmt(n_occupation, "Q2045208"),
  "Fitness Model": stmt(n_occupation, "Q58891836"),
  "Fitness Athlete": stmt(n_occupation, "Q58891836"),
  "Fitness Celebrity": stmt(n_occupation, "Q58891836"),
  "Naked": stmt(n_occupation, "Q161850"),
  "Nude": stmt(n_occupation, "Q161850"),
  "Erotic Model": stmt(n_occupation, "Q3286043"),
  "Twitch Streamer": stmt(n_occupation, "Q50279140"),
  "TikTok Star": stmt(n_occupation, "Q94791573"),
  "Instagram Model": stmt(n_occupation, "Q110990999"),
  "Insta": stmt(n_occupation, "Q110990999"),
  "InstaModel": stmt(n_occupation, "Q110990999"),
  "Instagram Star": stmt(n_occupation, "Q110990999"),
  "Youtuber": stmt(n_occupation, "Q17125263"),
  "Comedian": stmt(n_occupation, "Q245068"),
  "Pageant Contestant": stmt(n_occupation, "Q18581305"),
  "Beauty Queen": stmt(n_occupation, "Q18581305"),
  "Plus Size": stmt(n_occupation, "Q3286049"),
  "Dancer": stmt(n_occupation, "Q5716684"),
  "Ballerina": stmt(n_occupation, "Q805221"),
  "Blogger": stmt(n_occupation, "Q8246794"),
  "Influencer": stmt(n_occupation, "Q2906862"),
  "Cosplayer": stmt(n_occupation, "Q18810049"),
  "Singer-songwriter": stmt(n_occupation, "Q488205"),
  "Journalist": stmt(n_occupation, "Q1930187"),
  "Reporter": stmt(n_occupation, "Q42909"),
  "TV Presenter": stmt(n_occupation, "Q947873"),
  "Athlete": stmt(n_occupation, "Q2066131"),
  "Golfer": stmt(n_occupation, "Q11303721"),
  "Standup Comedian": stmt(n_occupation, "Q18545066"),
  "Camgirl": stmt(n_occupation, "Q1027930"),
  "Tv Host": stmt(n_occupation, "Q947873"),
  "Reality Tv": stmt(n_occupation, "Q27658988"),
  "Erotic Model": stmt(n_occupation, "Q3286043"),
  "Erotic Actress": stmt(n_occupation, "Q488111"),
  "P*rn Star": stmt(n_occupation, "Q488111"),
  "PMOM": stmt(n_occupation, "Q728711"),
  "Singer": stmt(n_occupation, "Q177220"),
  "Pin Up Model": stmt(n_occupation, "Q151092"),
  "Pin Up": stmt(n_occupation, "Q151092"),
  "Showgirl": stmt(n_occupation, "Q3482594"),

  "Blonde": stmt(n_hair_color, "Q202466"),
  "Blond": stmt(n_hair_color, "Q202466"),
  "Blonde Hair": stmt(n_hair_color, "Q202466"),
  "Dark Blonde": stmt(n_hair_color, "Q28868833"),
  "Strawberry Blonde": stmt(n_hair_color, "Q18661358"),
  "Brunette": stmt(n_hair_color, "Q2367101"),
  "Hair Brown": stmt(n_hair_color, "Q2367101"),
  "Brown-Haired": stmt(n_hair_color, "Q2367101"),
  "Light Brown Hair": stmt(n_hair_color, "Q79483632"),
  "Pink Hair": stmt(n_hair_color, "Q28962042"),
  "Black Hair": stmt(n_hair_color, "Q1922956"),
  "Raven-black Haired": stmt(n_hair_color, "Q1922956"),
  "Raven Haired": stmt(n_hair_color, "Q1922956"),
  "Redhead": stmt(n_hair_color, "Q152357"),
  "Red Head": stmt(n_hair_color, "Q152357"),
  "Red Hair": stmt(n_hair_color, "Q152357"),
  "Ginger": stmt(n_hair_color, "Q152357"),
  "Auburn Hair": stmt(n_hair_color, "Q2419551"),

  "Blue Eyes": stmt(n_eye_color, "Q17122834"),
  "Eyes Blue": stmt(n_eye_color, "Q17122834"),
  "Green Eyes": stmt(n_eye_color, "Q17122854"),
  "Eyes Green": stmt(n_eye_color, "Q17122854"),
  "Brown Eyes": stmt(n_eye_color, "Q17122705"),
  "Eyes Brown": stmt(n_eye_color, "Q17122705"),
  "Brown-eyed": stmt(n_eye_color, "Q17122705"),
  "Brown Eyed": stmt(n_eye_color, "Q17122705"),
  "Hazel Eyes": stmt(n_eye_color, "Q17122740"),
  "Eyes Hazel": stmt(n_eye_color, "Q17122740"),
  "Blue/green Eyes": stmt(n_eye_color, "Q3375649"),
  "Blue/grey Eyes": stmt(n_eye_color, "Q42845936"),

  "Lesbian": stmt(n_sexual_orientation, "Q6649"),
  "Suicide": stmt(n_manner_of_death, "Q10737"),
}

const anchors = new Set(["IMDB profile"]);

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

function date2sling(d) {
  return d.getFullYear() * 10000 + (d.getMonth() + 1) * 100 + d.getDate();
}

export default class ListalPlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let m = url.pathname.match(/^\/([^\/]+)/);
    if (!m) return;
    let modelid = decodeURIComponent(m[1]);
    if (!modelid) return;

    if (action == SEARCHURL) {
      return {
        ref: modelid,
        name: modelid.replace(/-/g, " "),
        description: "Listal model",
        url: url.href,
        context: context,
        onitem: item => this.select(item),
      };
    } else if (action == PASTEURL) {
      if (url.pathname.match(/^\/[^\/]+\/pictures/)) {
        await this.gallery(context, context.topic, url.href, modelid);
      } else {
        await this.populate(context, context.topic, url.href, modelid);
      }
      return true;
    }
  }

  async select(item) {
    // Create new topic.
    let topic = await item.context.new_topic();
    if (!topic) return;

    // Fetch profile from listal and populate topic.
    await this.populate(item.context, topic, item.url, item.ref);
  }

  async populate(context, topic, url, modelid) {
    // Retrieve listal profile for model.
    let r = await fetch(context.proxy(url), {headers: {
      "XUser-Agent": navigator.userAgent,
    }});
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");

    // Fields.
    let fields = new Map();
    let page = doc.querySelector("#itempagewrapper");
    for (let para of page.getElementsByTagName("p")) {
      let txt = para.innerText;
      let colon = txt.indexOf(':');
      if (colon == -1) continue;
      let field = txt.substring(0, colon).trim();
      let value = txt.substring(colon + 1).trim();
      fields.set(field, value);
    }

    // Tags.
    let tags = new Set();
    let alltags = doc.getElementById("alltags");
    if (alltags) {
      for (let link of alltags.getElementsByTagName("a")) {
        let tag = link.innerText;
        tags.add(tag);
      }
    }

    // Name.
    let name = doc.querySelector('h1[itemprop="name"]');
    if (name) {
      name = name.innerText.trim();
      topic.put(n_name, name);
    }
    let bname = fields.get("Birth Name");
    if (bname && bname != name) topic.put(n_birth_name, bname);

    // Type.
    topic.put(n_instance_of, n_human);

    // Gender.
    if (tags.has("Female") || tags.has("Female Model")) {
      topic.put(n_gender, n_female);
    } else if (tags.has("Male") || tags.has("Male Model")) {
      topic.put(n_gender, n_male);
    } else {
      topic.put(n_gender, n_female);
    }

    // Age.
    let age = fields.get("Age");
    if (age) {
      let m = age.match(/^\d+, born (\d+ \w+ \d+)$/);
      if (m) {
        let dob = new Date(m[1]);
        topic.put(n_date_of_birth, date2sling(dob));
      }
    }
    let born = fields.get("Born");
    if (born) {
      let m = born.match(/^(\d+ \w+ \d+) Died: (\d+ \w+ \d+)$/);
      if (m) {
        let dob = new Date(m[1]);
        topic.put(n_date_of_birth, date2sling(dob));
        let dod = new Date(m[2]);
        topic.put(n_date_of_death, date2sling(dod));
      }
    }

    // Country.
    let country = fields.get("Country of origin");
    if (country) {
      country = await lookup(context, country);
      if (!topic.has(n_place_of_birth, country)) {
        topic.put(n_country_of_citizenship, country);
      }
    }
    let place = fields.get("Born and residing in");
    if (place) {
      place = await lookup(context, place);
      if (!topic.has(n_place_of_birth, country)) {
        topic.put(n_country_of_citizenship, place);
      }
    }

    // Residence.
    let residence = fields.get("Currently Residing In");
    if (residence) {
      residence = await lookup(context, residence);
      topic.put(n_residence, residence);
    }

    // Partner.
    let partner = fields.get("Partner");
    if (partner) {
      if (fields.get("Relationship Status") == "Married") {
        topic.put(n_spouse, partner);
      } else {
        topic.put(n_partner, partner);
      }
    }

    // Add tag properties.
    for (let tag of tags) {
      let m = tagsdir[tag];
      if (m) topic.put(m[0], m[1]);
    }

    // Height.
    let height = fields.get("Height");
    if (height && !topic.has(n_height)) {
      let m = height.match(/^(\d+)' (\d+)"$/);
      if (m) {
        let feet = parseFloat(m[1]);
        let inches = parseFloat(m[2]);
        let cm = Math.round(feet * 30.48 + inches * 2.54)
        let v = store.frame();
        v.add(n_amount, cm);
        v.add(n_unit, n_cm);
        topic.put(n_height, v);
      }
    }

    // Listal model ID.
    topic.put(n_listal, modelid);

    // Social links.
    let social = new SocialTopic(topic, context);
    for (let link of page.getElementsByTagName("a")) {
      let anchor = link.innerText;
      if (anchors.has(anchor)) {
        let url = link.getAttribute("href");
        await social.add_link(url);
      }
    }

    // Profile picture.
    let img = page.querySelector("img.pure-img");
    if (img) {
      let src = img.getAttribute("src");
      let qs = src.indexOf('?');
      if (qs != -1) src = src.substring(0, qs);
      if (src != "https://lthumb.lisimg.com/0/0.jpg") {
        topic.put(n_media, src);
      }
    }

    context.updated(topic);
  }

  async gallery(context, topic, url, modelid) {
    // Fetch all picture pages.
    let images = new Array();
    let done = false;
    for (let page = 1; !done; ++page) {
      let pageurl = `https://www.listal.com/${modelid}/pictures/${page}`;
      let r = await fetch(context.proxy(pageurl), {
        headers: {
          "XUser-Agent": navigator.userAgent,
        },
      });
      let html = await r.text();
      let doc = new DOMParser().parseFromString(html, "text/html");

      let imageboxes = doc.querySelectorAll("div.imagebox");
      for (let imagebox of imageboxes.values()) {
        let img = imagebox.querySelector("img");
        if (!img) continue;
        let src = img.getAttribute("src");
        if (!src) continue;
        let pos = src.indexOf('?');
        if (pos > 0) src = src.substring(0, pos);
        if (images.includes(src)) continue;
        images.push(src);
      }
      done = imageboxes.length == 0;
    }

    // Add pictures to topic.
    topic.put(n_listal, modelid);
    for (let image of images) topic.put(n_media, image);

    context.updated(topic);
  }
};


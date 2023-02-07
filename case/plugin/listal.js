// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from listal.com.

import {store, frame} from "/case/app/global.js";
import {SocialTopic} from "/case/app/social.js";
import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";

const n_name = frame("name");
const n_birth_name = frame("P1477");
const n_instance_of = frame("P31");
const n_human = frame("Q5");
const n_date_of_birth = frame("P569");
const n_date_of_death = frame("P570");
const n_place_of_birth = frame("P19");
const n_country_of_citizenship = frame("P27");
const n_residence = frame("P551");
const n_gender = frame("P21");
const n_female = frame("Q6581072");
const n_male = frame("Q6581097");
const n_listal = frame("P11386");
const n_height = frame("P2048");
const n_amount = frame("/w/amount");
const n_unit = frame("/w/unit");
const n_cm = frame("Q174728");
const n_spouse = frame("P26");
const n_partner = frame("P451");
const n_media = frame("media");
const n_occupation = frame("P106");
const n_hair_color = frame("P1884");
const n_eye_color = frame("P1340");
const n_manner_of_death = frame("P1196");
const n_sexual_orientation = frame("P91");

function stmt(prop, value) {
  return [prop, frame(value)];
}

const tagsdir = {
  "Actor": stmt(n_occupation, "Q33999"),
  "Actress": stmt(n_occupation, "Q33999"),
  "Anchor": stmt(n_occupation, "Q270389"),
  "Athlete": stmt(n_occupation, "Q2066131"),
  "Ballerina": stmt(n_occupation, "Q805221"),
  "Beauty Queen": stmt(n_occupation, "Q18581305"),
  "Blogger": stmt(n_occupation, "Q8246794"),
  "Bodybuilder": stmt(n_occupation, "Q15982795"),
  "Burlesque": stmt(n_occupation, "Q107199096"),
  "Camgirl": stmt(n_occupation, "Q1027930"),
  "Chef": stmt(n_occupation, "Q3499072"),
  "Child Actress": stmt(n_occupation, "Q970153"),
  "Comedian": stmt(n_occupation, "Q245068"),
  "Cosplayer": stmt(n_occupation, "Q18810049"),
  "Dancer": stmt(n_occupation, "Q5716684"),
  "Director": stmt(n_occupation, "Q3455803"),
  "Dj": stmt(n_occupation, "Q130857"),
  "Erotic Actress": stmt(n_occupation, "Q488111"),
  "Erotic Model": stmt(n_occupation, "Q3286043"),
  "Erotic Model": stmt(n_occupation, "Q3286043"),
  "Fashion Model": stmt(n_occupation, "Q3357567"),
  "Fashion Designer": stmt(n_occupation, "Q3501317"),
  "Film Actress": stmt(n_occupation, "Q10800557"),
  "Filmmaker": stmt(n_occupation, "Q1414443"),
  "Fitness Athlete": stmt(n_occupation, "Q58891836"),
  "Fitness Celebrity": stmt(n_occupation, "Q58891836"),
  "Fitness Model": stmt(n_occupation, "Q58891836"),
  "Golfer": stmt(n_occupation, "Q11303721"),
  "Gravure": stmt(n_occupation, "Q1328668"),
  "Gravure Idol": stmt(n_occupation, "Q1328668"),
  "Influencer": stmt(n_occupation, "Q2906862"),
  "Instagram Model": stmt(n_occupation, "Q110990999"),
  "Instagram Star": stmt(n_occupation, "Q110990999"),
  "InstaModel": stmt(n_occupation, "Q110990999"),
  "Insta": stmt(n_occupation, "Q110990999"),
  "Journalist": stmt(n_occupation, "Q1930187"),
  "Model": stmt(n_occupation, "Q4610556"),
  "Naked": stmt(n_occupation, "Q161850"),
  "Nude": stmt(n_occupation, "Q161850"),
  "Pageant Contestant": stmt(n_occupation, "Q18581305"),
  "Pin Up Model": stmt(n_occupation, "Q151092"),
  "Pin Up": stmt(n_occupation, "Q151092"),
  "PMOM": stmt(n_occupation, "Q728711"),
  "P*rn Star": stmt(n_occupation, "Q488111"),
  "Reality Tv": stmt(n_occupation, "Q27658988"),
  "Reality Tv Contestant": stmt(n_occupation, "Q27658988"),
  "Realtor": stmt(n_occupation, "Q519076"),
  "Reporter": stmt(n_occupation, "Q42909"),
  "Ring Girl": stmt(n_occupation, "Q922176"),
  "Seiyuu": stmt(n_occupation, "Q622807"),
  "Showgirl": stmt(n_occupation, "Q3482594"),
  "Singer-songwriter": stmt(n_occupation, "Q488205"),
  "Singer": stmt(n_occupation, "Q177220"),
  "Social Media Star": stmt(n_occupation, "Q2045208"),
  "Standup Comedian": stmt(n_occupation, "Q18545066"),
  "Stripper": stmt(n_occupation, "Q1141526"),
  "Supermodel": stmt(n_occupation, "Q865851"),
  "Tarento": stmt(n_occupation, "Q2705098"),
  "Television Actress": stmt(n_occupation, "Q10798782"),
  "TikTok Star": stmt(n_occupation, "Q94791573"),
  "Tv Actress": stmt(n_occupation, "Q10798782"),
  "Tv Host": stmt(n_occupation, "Q947873"),
  "TV Presenter": stmt(n_occupation, "Q947873"),
  "Twitch Streamer": stmt(n_occupation, "Q50279140"),
  "Writer": stmt(n_occupation, "Q36180"),
  "Youtuber": stmt(n_occupation, "Q17125263"),

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
    return frame(data.matches[0].ref);
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
    let r = await context.fetch(url);
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
    let social = new SocialTopic(topic, context);
    await social.add_prop(n_listal, modelid);

    // Social links.
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
      let r = await context.fetch(pageurl);
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


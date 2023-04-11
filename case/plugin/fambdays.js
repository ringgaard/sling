// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for famousbirthdays.com.

import {store, frame} from "/case/app/global.js";
import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";
import {date_parser} from "/case/app/value.js";

const n_name = frame("name");
const n_description = frame("description");
const n_instance_of = frame("P31");
const n_human = frame("Q5");
const n_date_of_birth = frame("P569");
const n_place_of_birth = frame("P19");
const n_occupation = frame("P106");
const n_famousbdays = frame("P11194");

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

function element_text(parent, selector) {
  return parent.querySelector(selector).innerText.trim();
}

export default class FamousBirthdaysPlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let m = url.pathname.match(/^\/people\/(.+?)\.html/);
    if (!m) return;
    let person = m[1];
    if (!person) return;

    if (action == SEARCHURL) {
      return {
        ref: person,
        name: person,
        description: "Famous Birthdays profile",
        context: context,
        onitem: item => this.select(item),
      };
    } else if (action == PASTEURL) {
      await this.populate(context, context.topic, person);
      return true;
    }
  }

  async select(item) {
    // Create new topic.
    let topic = await item.context.new_topic();
    if (!topic) return;

    // Fetch profile from famousbirthdays.com and populate topic.
    await this.populate(item.context, topic, item.ref);
  }

  async populate(context, topic, person) {
    // Retrieve profile for person.
    let url = `https://famousbirthdays.com/people/${person}.html`;
    let r = await fetch(context.proxy(url));
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");
    let main = doc.querySelector("div.bio-module__info");

    // Get bio.
    let name = element_text(main, "span.bio-module__first-name");
    let title = element_text(main, "p.bio-module__profession");
    let stats = main.querySelectorAll("div.bio-module__person-attributes p");

    let bday = stats[0].innerText.trim();
    let m = bday.match(/^(Happy )?Birthday!?\s+([A-Z][a-z]+)\w* (\d+), (\d+)/);
    let results = new Array();
    date_parser(`${m[2]} ${m[3]}, ${m[4]}`, results);
    let dob = results[0].value

    let place = stats[2].innerText.replace(/\s+/g, " ").trim();
    m = place.match(/^Birthplace\s+(.+)/)
    let pob = await lookup(context, m[1].trim());

    let occ = await lookup(context, title);

    // Add bio to topic.
    topic.put(n_name, name);
    topic.put(n_instance_of, n_human);
    topic.put(n_date_of_birth, dob);
    topic.put(n_place_of_birth, pob);
    topic.put(n_occupation, occ);
    topic.put(n_famousbdays, "people/" + person);

    context.updated(topic);
  }
}


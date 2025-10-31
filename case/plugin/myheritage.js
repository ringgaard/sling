// Copyright 2025 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for MyHeritage search.

import {store, frame} from "/common/lib/global.js";
import {date_parser, Time} from "/common/lib/datatype.js";
import {match_link} from "/case/app/social.js";
import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";

const n_is = store.is;
const n_name = frame("name");
const n_dob = frame("P569");
const n_dod = frame("P570");
const n_birth_name = frame("P1477");
const n_married_name = frame("P2562");
const n_mh_id = frame("PMH");
const n_pob = frame("P19");
const n_pod = frame("P20");
const n_father = frame("P22");
const n_mother = frame("P25");
const n_parent = frame("P8810");
const n_spouse = frame("P26");
const n_sibling = frame("P3373");
const n_child = frame("P40");
const n_gender = frame("P21");
const n_female = frame("Q6581072");
const n_male = frame("Q6581097");
const n_instance_of = frame("P31");
const n_human = frame("Q5");
const n_occupation = frame("P106");
const n_start_time = frame("P580");
const n_described_at_url = frame("P973");
const n_marriage_place = frame("P2842");

const collection_properties = {
  1: frame("PMHI"),
  10187: frame("PDFT01"),
  10253: frame("PDFT06"),
  10184: frame("PDFT11"),
  10196: frame("PDFT16"),
  10197: frame("PDFT21"),
  10190: frame("PDFT25"),
  10181: frame("PDFT30"),
  10706: frame("PDFT40"),
}

function is_date(d) {
  let results = new Array();
  date_parser(d, results);
  return results.length > 0;
}

function parse_date(d) {
  if (!d) return undefined;
  let results = new Array();
  date_parser(d, results);
  if (results.length > 0) return results[0].value;
  return d;
}

function parse_record(element) {
  if (!element) return;
  let fields = {};
  for (let n of element.childNodes) {
    if (n.nodeType == Node.ELEMENT_NODE) {
      if (n.tagName == "SPAN") {
        //console.log(n);
        let id = n.getAttribute("data-item-id");
        if (n.className == "event_date") {
          fields.date = parse_date(n.innerText);
        } else if (n.className == "event_place") {
          let place = n.querySelector("span.map_callout_link");
          if (place) fields.location = place.innerText?.trim();
        } else if (id) {
          let text = n.innerText?.trim();
          fields.id = id;
          if (text) fields.name = text;
        } else {
          let text = n.innerText?.trim();
          if (text) fields.comment = text;
        }
      } else if (n.tagName == "DIV") {
        if (n.className == "mapCalloutContainer") {
          let place = n.querySelector("span.map_callout_link");
          if (place) fields.location = place.innerText?.trim();
        }
      } else if (n.tagName == "A") {
        let name = n.innerText?.trim();
        if (name) fields.name = name;
      }
    } else if (n.nodeType == Node.TEXT_NODE) {
      let text = n.wholeText?.trim();
      if (text) {
        if (is_date(text)) {
          fields.date = parse_date(text);
        } else {
          fields.text = text;
        }
      }
    }
  }
  return fields;
}

function parse_list(elem) {
  if (!elem) return;
  let values = [];
  for (let e of elem.children) {
    let id = e.getAttribute("data-item-id");
    if (!id) continue;
    if (id.endsWith("-")) id = id.slice(0, -1);

    let value = parse_record(e);
    value.id = id;
    values.push(value);
  }
  return values;
}

function parse_source(elem) {
  if (!elem) return;
  elem = elem.querySelector("a");
  if (elem) {
    return elem.getAttribute("href").trim();
  }
}

export default class MyHeritagePlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let m = url.pathname.match(/\/collection-(\d+)\//);
    if (!m) return;
    let collection = parseInt(m[1]);
    let params = new URLSearchParams(url.search);
    let itemid = params.get("itemId");

    if (action == SEARCHURL) {
      return {
        name: `${itemid} in collection ${collection}`,
        description: "MyHeritage item",
        url: url.href,
        collection: collection,
        itemid: itemid,
        context: context,
        onitem: item => this.select(item),
      };
    } else if (action == PASTEURL) {
      await this.populate(context, context.topic, query, collection, itemid);
      return true;
    }
  }

  async select(item) {
    // Create new topic.
    let topic = await item.context.new_topic();
    if (!topic) return;

    // Fetch profile from MyHeritage and populate topic.
    await this.populate(item.context, topic, item.url,
                        item.collection, item.itemid);
  }

  async populate(context, topic, url, collection, itemid) {
    // Retrieve MyHeritage profile.
    let r = await context.fetch(url, {local: true});
    if (r) {
      // Parse HTML.
      let html = await r.text();
      let doc = new DOMParser().parseFromString(html, "text/html");
      let table = doc.querySelector("table.recordFieldsTable");
      //console.log(table);

      // Get fields.
      let fields = {}
      let rows = table.querySelectorAll("tr.recordFieldsRow");
      for (let f of rows.values()) {
        let field_name = f.getAttribute("data-field-id");
        let field_value = f.querySelector("td.recordFieldValue");
        fields[field_name] = field_value;
        console.log(field_name, field_value);
      }

      // Populate bio.
      let bio = {};
      bio.name = parse_record(fields["NAME"])?.text;
      bio.married_name = parse_record(fields["married-name"])?.text;
      bio.birth_name = parse_record(fields["birth-name"])?.text;

      bio.gender = parse_record(fields["gender"])?.text ||
                   parse_record(fields["person-canonical-events.gender"])?.text;

      bio.birth = parse_record(fields["birth"]) ||
                  parse_record(fields["person-events.birth"]) ||
                  parse_record(fields["BIRT"]);

      bio.death = parse_record(fields["death"]) ||
                  parse_record(fields["person-events.death"]) ||
                  parse_record(fields["DEAT"]);

                  parse_record(fields["person-events.birth"]);

      bio.occupation = parse_record(fields["occupation"]) ||
                       parse_record(fields["OCCU"]);
      bio.father = parse_record(fields["father"]);
      bio.mother = parse_record(fields["mother"]);
      bio.parents = parse_list(fields["parents"]);
      bio.spouse = parse_record(fields["wife"]) ||
                   parse_record(fields["husband"]) ||
                   parse_record(fields["spouse"]);
      bio.children = parse_list(fields["children"]);
      bio.siblings = parse_list(fields["siblings"]) ||
                     parse_list(fields["sibling"]);
      let marriage = parse_record(fields["marriage"]) ||
                     parse_record(fields["marriage-array0"]) ||
                     parse_record(fields["MARR"]);
      if (marriage) {
        console.log("marriage", marriage);
        let spouse = marriage.name || marriage.text;
        if (spouse) {
          if (spouse.startsWith("Ægteskab med: ")) spouse = spouse.slice(14);
          if (spouse.startsWith("Ægtefælle: ")) spouse = spouse.slice(11);
          bio.spouse = spouse;
        }
        bio.married = marriage.date;
        bio.wedding = marriage.location;
      }
      bio.source = parse_source(fields["source-link"] || fields["source"]);
      console.log(bio);

      if (topic.has(n_name)) {
        if (topic.get(n_name) != bio.name) {
          topic.put(n_birth_name, bio.name);
        }
      } else {
        topic.put(n_name, bio.name);
      }
      topic.put(n_birth_name, bio.birth_name);
      topic.put(n_married_name, bio.married_name);
      topic.put(n_instance_of, n_human);
      if (bio.gender == "Mand") topic.put(n_gender, n_male);
      if (bio.gender == "Kvinde") topic.put(n_gender, n_female);
      topic.put(n_dob, bio.birth?.date);
      topic.put(n_pob, bio.birth?.location);
      topic.put(n_dod, bio.death?.date);
      topic.put(n_pod, bio.death?.location);

      topic.put(n_father, bio.father?.name);
      topic.put(n_mother, bio.mother?.name);
      if (bio.parents) {
        for (let p of bio.parents) {
          topic.put(n_parent, p.name);
        }
      }
      if (bio.siblings) {
        for (let s of bio.siblings) {
          topic.put(n_sibling, s.name);
        }
      }
      if (bio.spouse) {
        if (bio.married) {
          let marriage = store.frame();
          marriage.add(n_is, bio.spouse.name || bio.spouse);
          marriage.add(n_start_time, bio.married);
          if (bio.wedding) marriage.add(n_marriage_place, bio.wedding);
          topic.put(n_spouse, marriage);
        } else {
          topic.put(n_spouse, bio.spouse.name);
        }
      }
      if (bio.children) {
        for (let c of bio.children) {
          topic.put(n_child, c.name || c.text);
        }
      }
      topic.put(n_occupation, bio.occupation?.name || bio.occupation?.text);

      if (bio.source) {
        let [xprop, identifier] = match_link(bio.source);
        if (xprop) {
          topic.put(xprop, identifier);
        } else {
          topic.put(n_described_at_url, bio.source);
        }
      }
    }

    // Add reference.
    if (itemid.endsWith("-")) {
      itemid = itemid.slice(0, -1);
    }
    let prop = collection_properties[collection];
    if (prop) {
      topic.put(prop, itemid);
    } else {
      topic.put(n_mh_id, `${collection}:${itemid}`);
    }
    context.updated(topic);
 }

  async run(topic) {
    function escape(str) {
      return str.replaceAll(" ", "%2F3").replaceAll(".", "%2F2");
    }

    // Build search query for myheritage.dk.
    let url = "https://www.myheritage.dk/research?";
    url += "s=1&formId=master&formMode=1&useTranslation=1";
    url += "&exactSearch=&p=1&action=query&view_mode=card";
    let name = topic.get(n_name)?.toString();
    let birth_name = topic.get(n_birth_name)?.toString();
    let married_name = topic.get(n_married_name)?.toString();
    let dob = new Time(store.resolve(topic.get(n_dob)));
    let dod = new Time(store.resolve(topic.get(n_dod)));
    if (!name) name = birth_name || married_name;
    if (name) {
      let fn, ln;
      let space = name.lastIndexOf(" ");
      if (space != -1) {
        fn = name.substring(0, space);
        ln = name.substring(space + 1);
      } else {
        ln = name
      }
      if (birth_name && name != birth_name) {
        let space = birth_name.lastIndexOf(" ");
        if (space != -1) {
          let maiden_name = birth_name.substring(space + 1);
          if (!ln.includes(maiden_name)) {
            ln += "/" + maiden_name;
          }
        }

      }
      if (married_name && name != married_name) {
        let space = married_name.lastIndexOf(" ");
        if (space != -1) {
          let taken_name = married_name.substring(space + 1);
          if (!ln.includes(taken_name)) {
            ln = taken_name + "/" + ln;
          }
        }
      }
      url += "&qname=Name";
      if (fn) url += "+fn." + escape(fn);
      if (ln) url += "+ln." + escape(ln);
    }

    if (dob.precision) {
      url += "&qevents-event1=Event+et.birth";
      if (dob.day) url += "+ed." + dob.day;
      if (dob.month) url += "+em." + dob.month;
      if (dob.year) url += "+ey." + dob.year;
    }
    if (dod.precision) {
      url += "&qevents-any/1event_1=Event+et.death";
      if (dod.day) url += "+ed." + dod.day;
      if (dod.month) url += "+em." + dod.month;
      if (dod.year) url += "+ey." + dod.year;
    }
    url += "&qevents=List";
    window.open(url, "_blank", "noreferrer");
  }
}

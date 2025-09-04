// Copyright 2025 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for MyHeritage search.

import {store, frame} from "/common/lib/global.js";
import {Time} from "/common/lib/datatype.js";
import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";

const n_name = frame("name");
const n_dob = frame("P569");
const n_dod = frame("P570");
const n_birth_name = frame("P1477");
const n_married_name = frame("P2562");
const n_mh_id = frame("PMH");

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
    if (itemid.endsWith("-")) {
      itemid = itemid.slice(0, -1);
    }
    let prop = collection_properties[collection];
    if (prop) {
      topic.put(cp.prop, itemid);
    } else {
      topic.put(n_mh_id, `${collection}:${itemid}`);
    }
    context.updated(topic);

/*
    // Retrieve MyHeritage profile.
    let r = await context.fetch(url);
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");
    console.log(doc);
*/
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

// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from OpenCorporates.

import {store, frame} from "/case/app/global.js";
import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";

const n_is = store.is;
const n_name = frame("name");
const n_alias = frame("alias");
const n_description = frame("description");
const n_instance_of = frame("P31");
const n_business = frame("Q4830453");
const n_legal_form = frame("P1454");
const n_inception = frame("P571");
const n_dissolved = frame("P576");
const n_other_name = frame("P2561");
const n_start_time = frame("P580");
const n_end_time = frame("P582");
const n_headquarters_location = frame("P159")
const n_located_at_street_address = frame("P6375");
const n_postal_code = frame("P281");
const n_country = frame("P17");
const n_location = frame("P276");
const n_position_held = frame("P39");
const n_corporate_officer = frame("P2828");

const positions = {
  "director": frame("P1037"),
  "owner": frame("P127"),
  "adm. dir.": frame("P169"),
  "adm. dir": frame("P169"),
  "reel ejer": frame("P127"),
  "stiftere": frame("P112"),
  "formand": frame("P488"),
  "bestyrelsesmedlem": frame("P3320"),
  "bestyrelse": frame("P3320"),
  "direktør": frame("P1037"),

};

const n_company = frame("company");
const n_regauth = frame("regauth");
const n_country_id = frame("country");
const n_jurisdiction_id = frame("jurisdiction");

const n_opencorporates_id = frame("P1320");
const n_described_by_source = frame("P1343");
const n_opencorp = frame("Q7095760");

function date2sling(date) {
  if (!date) return undefined;
  let m = date.match(/(\d+)-(\d+)-(\d+)/);
  if (!m) return undefined;
  return parseInt(m[1]) * 10000 + parseInt(m[2]) * 100 + parseInt(m[3]);
}

export default class OpenCorpPlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let m = url.pathname.match(/^\/companies\/(\w+\/.+)/);
    if (!m) return;
    let ocid = m[1];
    if (!ocid) return;

    if (action == SEARCHURL) {
      return {
        ref: ocid,
        name: "Compary number " + ocid,
        description: "OpenCorporates company",
        context: context,
        onitem: item => this.select(item),
      };
    } else if (action == PASTEURL) {
      await this.populate(context, context.topic, ocid);
      return true;
    }
  }

  async select(item) {
    // Create new topic.
    let topic = await item.context.new_topic();
    if (!topic) return;

    // Add OpenCorporates information to topic.
    await this.populate(item.context, topic, item.ref);
  }

  async populate(context, topic, ocid) {
    // Fetch company information from OpenCorporates.
    let r = await fetch(context.service("opencorp", {ocid}));
    if (!r.ok) throw "Company not found";
    let json = await r.json();
    let company = json.results.company;
    console.log(company);

    // Name and description.
    topic.put(n_name, company.name);
    topic.put(n_description, "company");
    topic.put(n_instance_of, n_business);
    for (let n of company.previous_names) {
      let name = store.frame();
      name.add(n_is, n.company_name);
      name.put(n_start_time, date2sling(n.start_date));
      name.put(n_end_time, date2sling(n.end_date));
      topic.add(n_other_name, name);
    }
    for (let n of company.alternative_names) {
      topic.put(n_alias, n.company_name);
    }


    // Company life cycle.
    topic.put(n_legal_form, company.company_type);
    topic.put(n_inception, date2sling(company.incorporation_date));
    topic.put(n_dissolved, date2sling(company.dissolution_date));

    // Address.
    let addr = company.registered_address;
    if (addr) {
      let a = store.frame();
      a.add(n_is, addr.locality || addr.region || addr.country);
      if (addr.street_address) {
        let street = addr.street_address.replace("\n", ", ");
        a.put(n_located_at_street_address, street);
      }
      if (addr.postal_code) a.put(n_postal_code, addr.postal_code);
      if (addr.country) a.put(n_country, addr.country);
      topic.put(n_headquarters_location, a);
    }

    // Officers.
    for (let o of company.officers) {
      if (o.officer) {
        let prop = null;
        let p = store.frame();
        p.add(n_is, o.officer.name);
        if (o.officer.position) {
          prop = positions[o.officer.position.toLowerCase()];
          if (!prop) {
            p.add(n_position_held, o.officer.position);
          }
        }
        p.put(n_start_time, date2sling(o.officer.start_date));
        p.put(n_end_time, date2sling(o.officer.end_date));
        topic.put(prop || n_corporate_officer, p);
      }
    }

    // Add OpenCorporates company id.
    topic.put(n_opencorporates_id, ocid);
    let item = await context.idlookup(n_opencorporates_id, ocid);
    if (item) topic.put(n_is, item.id);

    // Get company ids.
    r = await fetch(context.service("biz", {ocid}));
    if (r.ok) {
      let reply = await store.parse(r);
      for (let [prop, value] of reply.get(n_company)) {
        if (!topic.has(prop, value)) {
          topic.put(prop, value);
          let item = await context.idlookup(prop, value);
          if (item) topic.put(n_is, item.id);
        }
      }
      let regauth = reply.get(n_regauth);
      let country = regauth.get(n_country_id);
      let region = regauth.get(n_jurisdiction_id);
      if (region && region != country) topic.put(n_location, region);
      if (country) topic.put(n_country, country);
    }

    // Add OpenCorporates as source.
    topic.put(n_described_by_source, n_opencorp);

    context.updated(topic);
  }
};


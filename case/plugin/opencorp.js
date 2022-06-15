// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from OpenCorporates.

import {store} from "/case/app/global.js";
import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";

const n_is = store.is;
const n_name = store.lookup("name");
const n_description = store.lookup("description");
const n_instance_of = store.lookup("P31");
const n_business = store.lookup("Q4830453");
const n_legal_form = store.lookup("P1454");
const n_inception = store.lookup("P571");
const n_dissolved = store.lookup("P576");
const n_other_name = store.lookup("P2561");
const n_start_time = store.lookup("P580");
const n_end_time = store.lookup("P582");
const n_headquarters_location = store.lookup("P159")
const n_located_at_street_address = store.lookup("P6375");
const n_postal_code = store.lookup("P281");
const n_country = store.lookup("P17");

/*
n_founded_by = kb["P112"]
n_founder_of = kb["Q65972149"]
n_owned_by = kb["P127"]
n_owner_of = kb["P1830"]
n_subsidiary = kb["P355"]
n_parent_organization = kb["P749"]
n_has_part = kb["P527"]
n_part_of = kb["P361"]
n_replaces = kb["P1365"]
n_replaced_by = kb["P1366"]
n_merged_into = kb["P7888"]
n_separated_from = kb["P807"]
n_corporate_officer = kb["P2828"]
n_employer = kb["P108"]
n_industry = kb["P452"]
n_chief_executive_officer = kb["P169"]
n_director_manager = kb["P1037"]
n_board_member = kb["P3320"]
n_chairperson = kb["P488"]
n_manager = kb["Q2462658"]
n_business_manager = kb["Q832136"]
n_opencorp = kb["P1320"]
n_described_by_source = kb["P1343"]
n_nace_code = kb["P4496"]
n_cvr = kb["Q795419"]
n_organization = kb["Q43229"]
n_foundation = kb["Q157031"]
n_association = kb["Q48204"]
n_human = kb["Q5"]
n_family_name = kb["Q101352"]
*/

const n_opencorporates_id = store.lookup("P1320");
const n_described_by_source = store.lookup("P1343");
const n_opencorp = store.lookup("Q7095760");

function date2sling(date) {
  if (!date) return undefined;
  let m = date.match(/(\d+)-(\d+)-(\d+)/);
  if (!m) return undefined;
  return parseInt(m[1]) * 10000 + parseInt(m[2]) * 100 + parseInt(m[3]);
}

export default class OpenCorpPlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let m = url.pathname.match(/^\/companies\/(\w+\/\w+)/);
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
    let url = `https://api.opencorporates.com/companies/${ocid}?format=json`;
    let r = await fetch(context.proxy(url));
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
      a.put(n_postal_code, addr.postal_code);
      topic.put(n_headquarters_location, a);
      topic.put(n_country, addr.country);
    }

    // Add OpenCorporates xref.
    topic.put(n_opencorporates_id, ocid);
    topic.put(n_described_by_source, n_opencorp);
    let item = await context.idlookup(n_opencorporates_id, ocid);
    if (item) topic.put(n_is, item.id);

    context.updated(topic);
  }
};


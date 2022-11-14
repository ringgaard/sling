// Copyright 2022 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from CVR (Danish Business Register).

import {store, frame, settings} from "/case/app/global.js";
import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";

const n_is = store.is;
const n_name = frame("name");

export default class CVRPlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let qs = url.searchParams;
    let unittype = qs.get("enhedstype");
    let cvrid = qs.get("id");
    if (!unittype || !cvrid) return;

    if (action == SEARCHURL) {
      return {
        ref: cvrid,
        unittype: unittype,
        name: `CVR ${unittype} ${cvrid}`,
        description: `CVR ${unittype}`,
        context: context,
        onitem: item => this.select(item),
      };
    } else if (action == PASTEURL) {
      await this.populate(context, context.topic, unittype, cvrid);
      return true;
    }
  }

  async select(item) {
    // Create new topic.
    let topic = await item.context.new_topic();
    if (!topic) return;

    // Link profile to CVR.
    await this.populate(item.context, topic, item.unittype, item.ref);
  }

  async populate(context, topic, unittype, cvrid) {

    // Determine item id based on CVR unit type.
    var itemid;
    if (unittype == "virksomhed") {
      itemid = `P1059/${cvrid}`;
    } else if (unittype == "person") {
      itemid = `P7972/${cvrid}`;
    } else if (unittype == "produktionsenhed") {
      itemid = `P2814/${cvrid}`;
    } else {
      itemid = `PCVR/${cvrid}`;
    }

    // Fetch item for CVR unit.
    let item = frame(itemid);
    if (!item.ispublic()) {
      let url = `${settings.kbservice}/kb/topic?id=${item.id}`;
      let response = await fetch(url);
      item = await store.parse(response);
    }

    // Add name and link to topic.
    let name = item.get(n_name);
    if (name) topic.put(n_name, name);
    topic.put(n_is, item);

    context.updated(topic);
  }
};


// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding history from glamourgirlsofthesilverscreen.com.

import {store, frame} from "/case/app/global.js";
import {date_parser} from "/case/app/value.js";

const n_is = store.is;
const n_time = frame("P585");

export default class GlamourGirlPlugin {
  async process(action, query, context) {
    // Fetch and parse page.
    let r = await fetch(context.proxy(query));
    let html = await r.text();
    let doc = new DOMParser().parseFromString(html, "text/html");

    // Add bio entries as comments.
    let tb = doc.getElementById("tb");
    if (!tb) return;

    let topic = context.topic;
    for (let tr of tb.getElementsByTagName("tr")) {
      let td = tr.getElementsByTagName("td");
      if (td.length != 2) continue;
      let time = td[0].innerText.trim();
      let event = td[1].innerText.trim();
      if (!time && !event) continue;
      if (time && time != "?") {
        if (time.match(/^\d\d$/)) time = "19" + time;
        let results = new Array();
        date_parser(time, results);
        if (results.length > 0) time = results[0].value;
        let f = store.frame();
        f.add(n_is, event);
        f.add(n_time, time);
        topic.add(null, f);
      } else {
        topic.add(null, event);
      }
    }

    context.updated(topic);
    return true;
  }
};


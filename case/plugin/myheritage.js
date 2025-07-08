// Copyright 2025 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for MyHeritage search.

import {store, frame} from "/common/lib/global.js";
import {Time} from "/common/lib/datatype.js";

const n_name = frame("name");
const n_dob = frame("P569");
const n_dod = frame("P570");
const n_birth_name = frame("P1477");

export default class MyHeritagePlugin {
  async run(topic) {
    function escape(str) {
      return str.replaceAll(" ", "%2F3").replaceAll(".", "%2F2");
    }

    // Build search query for myheritage.dk.
    let url = "https://www.myheritage.dk/research?";
    url += "s=1&formId=master&formMode=1&useTranslation=1";
    url += "&exactSearch=&p=1&action=query&view_mode=card";
    let name = topic.get(n_birth_name) || topic.get(n_name);
    if (name) name = name.toString();
    let dob = new Time(topic.get(n_dob));
    let dod = new Time(topic.get(n_dod));
    if (name) {
      let fn, ln;
      let space = name.lastIndexOf(" ");
      if (space) {
        fn = name.substring(0, space);
        ln = name.substring(space + 1);
      } else {
        ln = name
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

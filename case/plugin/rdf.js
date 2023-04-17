// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for pasting topic(s) in JSON-LD format.

import {Store, Frame} from "/common/lib/frame.js";
import {store, frame} from "/common/lib/global.js";
import {Time} from "/common/lib/datatype.js";

import {match_link} from "/case/app/social.js";

const property_mapping = {
  "PSCHM/description": "description",
  "PSCHM/headline": "name",
  "PSCHM/keywords": "Q4919820",
  "PSCHM/articleSection": "Q1931107",
  "PSCHM/thumbnailUrl": "media",
  "PSCHM/NewsMediaOrganization": "Q1193236",
  "PSCHM/dateModified": "P5017",
  "P2561": "name",
  "P18": "media",
}

const n_is = frame("is");
const n_isa = frame("isa");
const n_name = frame("name");
const n_media = frame("media");
const n_target = frame("target");
const n_url_type = frame("/w/url");
const n_media_type = frame("/w/media");

const n_instance_of = frame("P31");
const n_url = frame("P2699");
const n_exact_match = frame("P2888");

const n_date = frame("Q3016931");
const n_image_file = frame("Q860625");

let propmap = new Map();
for (const [key, value] of Object.entries(property_mapping)) {
  propmap.set(frame(key), frame(value));
}

function get_url(value) {
  if (value instanceof Frame) value = value.get(n_url) || value;
  if (value instanceof Frame) value = value.id || value;
  if (value instanceof Frame) value = store.transfer(value);
  return value;
}

export default class RDFPlugin {
  async process(action, data, context) {
    // Convert using RDF service.
    let r = await fetch(context.service("rdf"), {
      method: "POST",
      headers: {
        "Content-Type": "application/ld+json",
      },
      body: data,
    });
    if (!r.ok) throw `Error: ${r.statusText}`;

    let s = new Store(store);
    let objects = await s.parse(r);
    let existing = context.topic;
    for (let object of objects) {
      console.log(object.text(true));

      // Create new topic or use exising one.
      let topic = existing;
      existing = null;
      if (!topic) topic = await context.new_topic();

      // Import object into totpic.
      this.convert(object, topic);

      context.updated(topic);
    }
    return true;
  }

  // Import object into topic.
  convert(object, topic) {
    for (let [k, v] of object) {
      let prop = propmap.get(k) || store.transfer(k);
      let value;
      if (prop == n_isa) {
        prop = n_instance_of;
        value = store.transfer(v);
      } else if (prop == n_exact_match) {
        let id = v.id || v;
        let [xprop, identifier] = match_link(id);
        if (xprop) {
          prop = xprop;
          value = identifier;
        } else if (id.startsWith("http")) {
          value = id;
        } else {
          prop = n_is;
          value = store.transfer(v);
        }
      } else if (prop == n_media) {
          value = get_url(v);
      } else {
        let type = v.get && v.get(n_isa);
        if (type == n_date) {
          let t = new Time(v.get(n_is));
          value = t.value();
        } else if (type == n_image_file) {
          value = get_url(v);
        } else {
          let dt = prop.get(n_target);
          if (dt == n_url_type || dt == n_media_type) {
            value = get_url(v);
          } else if (v instanceof Frame) {
            if (v.isanonymous()) {
              value = store.frame();
              this.convert(v, value);
              if (!value.get(n_is)) {
                value.rename(n_name, n_is);
              }
            } else {
              value = store.transfer(v);
            }
          } else {
            value = store.transfer(v);
          }
        }
      }

      if (typeof(value) === "string") value = value.trim();
      if (value) topic.put(prop, value);
    }
  }
};


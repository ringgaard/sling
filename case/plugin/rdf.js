// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for pasting topic(s) in JSON-LD format.

import {store} from "/case/app/global.js";

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
    let topics = await store.parse(r);
    let existing = context.topic;
    console.log(topics);
    for (let t of topics) {
      let topic = existing;
      existing = null;
      if (!topic) topic = await context.new_topic();
      for (let [name, value] of t) {
        topic.add(name, value);
      }
      context.updated(topic);
    }
    return true;
  }
};


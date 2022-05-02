// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {store, settings, save_settings} from "./global.js";
import {Frame, Encoder} from "/common/lib/frame.js";

const n_is = store.is;
const n_topics = store.lookup("topics");
const n_created = store.lookup("created");
const n_results = store.lookup("results");

export async function wikidata_initiate() {
  // Initiate authorization and get redirect url.
  let callback = window.location.href;
  let r = await fetch("/case/wikibase/initiate?cb=" +
                      encodeURIComponent(callback));
  let response = await r.json()

  // Rememeber auth id.
  settings.wikidata_authid = response.authid;
  save_settings();

  // Redirect to let user authorize SLING.
  window.location.href = response.redirect;
}

export async function oauth_callback() {
  // Get auth id.
  let authid = settings.wikidata_authid;
  settings.wikidata_authid = null;

  // Get client access token.
  let authurl = encodeURIComponent(window.location.href);
  let url = `/case/wikibase/access?authid=${authid}&authurl=${authurl}`;
  let r = await fetch(url);
  let response = await r.json()
  if (!response.key) {
    console.log("Error accessing wikidata client token", response);
    return false;
  }

  // Store client token and secret.
  settings.wikidata_key = response.key;
  settings.wikidata_secret = response.secret;
  save_settings();

  return true;
}

export async function wikidata_export(topics, aux) {
  // Encode selected topics.
  let encoder = new Encoder(store);
  for (let topic of topics) {
    encoder.encode(topic);
  }

  // Add auxiliary topics.
  for (let topic of aux) {
    encoder.encode(topic);
  }

  let request = store.frame();
  request.add(n_topics, topics);
  encoder.encode(request);

  let r = await fetch("/case/wikibase/export", {
    method: "POST",
    headers: {
      "Client-Key": settings.wikidata_key,
      "Client-Secret": settings.wikidata_secret,
    },
    body: encoder.output()
  });

  if (r.status != 200) {
    console.log("Error exporting topics", r.status);
    throw new Error("Export to Wikidata failed");
  }

  let reply = await store.parse(r);
  console.log(reply.text());

  // Add QIDs to topics for newly created item.
  let updated = new Array();
  for (let [topic, item] of reply.get(n_created)) {
    topic.put(n_is, item.id);
    updated.push(topic);
  }
  let status = new Array();
  for (let [metric, value] of reply.get(n_results)) {
    if (value != 0) {
      status.push(`${value} ${metric.id}`);
    }
  }

  return [updated, status.join(", ")];
}


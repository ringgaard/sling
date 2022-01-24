// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {store, settings, save_settings} from "./global.js";

let apiurl = "https://test.wikidata.org/w/api.php"

async function wikidata_initiate() {
  // Initiate authorization and get redirect url.
  let callback = window.location.href;
  let r = await fetch("/case/wikibase/authorize?cb=" +
                      encodeURIComponent(callback));
  let response = await r.json()

  // Rememeber auth id.
  settings.wikidata_authid = response.authid;
  settings.wikidata_consumer = response.consumer;
  save_settings();

  // Redirect to let user authorize SLING.
  window.location.href = response.redirect;
}

export async function oauth_callback() {
  // Get auth id.
  let authid = settings.wikidata_authid;
  settings.wikidata_authid = null;

  // Get client access token.
  let params = window.location.search + "&authid=" + authid;
  let r = await fetch("/case/wikibase/access" + params);
  let response = await r.json()
  if (!response.key) {
    console.log("Error accessing wikidata client token", response);
    return false;
  }

  // Store client token and secret.
  settings.wikidata_key = response.key;
  settings.wikidata_secrect = response.secret;
  save_settings();

  return true;
}

export async function wikidata_export(casefile) {
  // Initiate OAuth authorization if we don't have an access token.
  if (!settings.wikidata_key) {
    wikidata_initiate();
    return;
  }

  console.log("wikidata client token", settings.wikidata_key);
}


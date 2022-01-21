// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";

import {store, settings} from "./global.js";

export async function wikidata_export(casefile) {
  let callback = window.location.href;
  let r = await fetch("/case/wikibase/initiate?cb=" +
                      encodeURIComponent(callback));
  let data = await r.json()
  console.log("initiate", data)
  window.location.href = data.url;
}


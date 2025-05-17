// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Store} from "./frame.js";

// Global SLING store.
export var store = new Store();

export function frame(id) {
  return store.lookup(id);
}

store.parse("{=P11079 =PLITR}").state = 0;
store.parse("{=P11386 =PLSTL}").state = 0;

// Global settings.
export var settings = {}

export function read_settings() {
  settings = JSON.parse(window.localStorage.getItem("settings") || "{}");
  if (!settings.kbservice) settings.kbservice = "";
  if (!settings.collaburl) settings.collaburl = "wss://ringgaard.com/collab/";
  if (!settings.cookiejar) settings.cookiejar = {};
}

export function save_settings() {
  window.localStorage.setItem("settings", JSON.stringify(settings));
}

export async function kbfetch(url, options) {
  let r = await fetch(settings.kbservice + url, options);
  if (!r.ok) {
    let error = r.headers.get("Error");
    if (!error) error = r.statusText;
    if (!error) error = `HTTP error ${r.status}`;
    throw new Error(error);
  }
  return r;
}

read_settings();

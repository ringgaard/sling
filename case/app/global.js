// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Store} from "/common/lib/frame.js";

// Global SLING store.
export var store = new Store();

// Global settings.
export var settings = {}

export function read_settings() {
  settings = JSON.parse(window.localStorage.getItem("settings") || "{}");
  if (!settings.kbservice) settings.kbservice = "https://ringgaard.com";
  if (!settings.collaburl) settings.collaburl = "wss://ringgaard.com/collab/";
}

export function save_settings() {
  window.localStorage.setItem("settings", JSON.stringify(settings));
}

read_settings();


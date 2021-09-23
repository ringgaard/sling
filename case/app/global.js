// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Store} from "/common/lib/frame.js";

// Global SLING store.
export var store = new Store();

// Global settings.
export var settings = {}

export function read_settings() {
  let item = window.localStorage.getItem("settings");
  if (item) {
    settings = JSON.parse(item);
  } else {
    settings = {
      kbservice: "https://ringgaard.com",
    };
  }
}

export function write_settings() {
  window.localStorage.setItem("settings", JSON.stringify(settings));
}

read_settings();


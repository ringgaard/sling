// Copyright 2022 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING importer plug-in for importing data from SLING frames.

import {Store, Frame, Encoder, Printer} from "/common/lib/frame.js";

export default class SLINGmporter {
  async process(file, context) {
    // Parse SLING data into new store.
    let data = await file.arrayBuffer()
    let store = new Store();
    let reader = new Reader(this, data);
    let frames = new Array();
    while (!reader.done()) {
      let obj = reader.parse();
      if (obj instanceof Frame) {
        frames.push(obj);
      } else (if obj instanceof Array) {
        frames.push(...obj);
      } else {
        throw "Invalid SLING frame file";
      }
    }

    throw "SLING import not yet implemented";
  }
};


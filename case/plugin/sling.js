// Copyright 2022 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING importer plug-in for importing data from SLING frames.

import {Frame, Reader, Decoder} from "/common/lib/frame.js";
import {store} from "/common/lib/global.js";

export default class SLINGImporter {
  async process(file, context) {
    // Use server-based bulk importer for collaborations.
    let data = await file.arrayBuffer();
    if (context.collab()) {
      await context.bulkimport(data);
    } else {
      // Parse SLING data.
      let frames = new Array();
      let bytes = new Uint8Array(data);
      if (bytes[0] == 0) {
        // Binary encoding.
        let decoder = new Decoder(store, data);
        while (!decoder.done()) {
          let obj = decoder.read();
          if (obj instanceof Frame) {
            frames.push(obj);
          } else if (obj instanceof Array) {
            frames.push(...obj);
          } else {
            throw "Invalid SLING frame file";
          }
        }
      } else {
        // Text encoding.
        let reader = new Reader(store, data);
        while (!reader.done()) {
          let obj = reader.parse();
          if (obj instanceof Frame) {
            frames.push(obj);
          } else if (obj instanceof Array) {
            frames.push(...obj);
          } else {
            console.log("unexpected object", obj);
            throw "Invalid SLING frame file";
          }
        }
      }

      // Add frames as topics.
      for (let frame of frames) {
        if (frame.ispublic()) store.unregister(frame);
        await context.new_topic(frame);
      }
    }
  }
};


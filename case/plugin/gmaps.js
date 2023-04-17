// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding geo coordinates from Google Maps.

import {store, frame, settings} from "/common/lib/global.js";

import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";

const n_isa = store.isa;
const n_name = frame("name");
const n_coordinate = frame("P625");
const n_geo = frame("/w/geo");
const n_lat = frame("/w/lat");
const n_lng = frame("/w/lng");

export default class GoogleMapsPlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let path = url.pathname;
    let m = path.match(/\/maps\/place\/([^\/]+)\/@(-?\d+\.\d+),(-?\d+\.\d+)/);
    let place = decodeURIComponent(m[1].replace(/\+/g, "%20"));
    let lat = parseFloat(m[2]);
    let lng = parseFloat(m[3]);

    if (action == SEARCHURL) {
      return {
        name: place,
        lat: lat,
        lng: lng,
        description: "geographic location",
        context: context,
        onitem: item => this.select(item),
      };
    } else if (action == PASTEURL) {
      await this.populate(context, context.topic, place, lat, lng);
      return true;
    }
  }

  async select(item) {
    // Create new location topic.
    let topic = await item.context.new_topic();
    if (!topic) return;
    await this.populate(item.context, topic, item.name, item.lat, item.lng);
  }

  async populate(context, topic, place, lat, lng) {
    // Add name if missing.
    if (place && !topic.has(n_name)) {
      topic.put(n_name, place);
    }

    // Add geo coordinate.
    let pos = store.frame();
    pos.add(n_isa, n_geo);
    pos.add(n_lat, lat);
    pos.add(n_lng, lng);
    topic.add(n_coordinate, pos);

    context.updated(topic);
  }
};


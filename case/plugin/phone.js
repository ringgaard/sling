// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding phone number.

import {store, frame} from "/common/lib/global.js";

const n_phone_number = frame("P1329");

export default class EmailPlugin {
  process(action, phone, context) {
    if (!context.topic) return false;

    context.topic.put(n_phone_number, phone);
    context.updated(context.topic);
    return true;
  }
};


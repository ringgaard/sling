// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding email address.

import {store, frame} from "/common/lib/global.js";

const n_email_address = frame("P968");

export default class EmailPlugin {
  process(action, email, context) {
    if (!context.topic) return false;

    context.topic.put(n_email_address, "mailto:" + email);
    context.updated(context.topic);
    return true;
  }
};


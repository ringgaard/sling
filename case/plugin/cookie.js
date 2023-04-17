// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding cookies to the cookie jar.

import {settings, save_settings} from "/common/lib/global.js";
import {inform} from "/common/lib/material.js";

export default class CookiePlugin {
  process(action, query, context) {
    let m = query.match(/^cookie:([^ ]+) (.+)$/);
    if (!m) return;
    let host = m[1];
    let cookies = m[2];
    settings.cookiejar[host] = cookies;
    save_settings();
    inform(`Credential cookies for ${host} added`);
  }
};


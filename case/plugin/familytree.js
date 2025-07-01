// Copyright 2026 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for family tree.

import {store, frame} from "/common/lib/global.js";

export default class FamilyTreePlugin {
  async run(topic) {
    console.log("family tree for " + topic.id);
  }
}

// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from LinkTree profile.

import {store} from "/case/app/global.js";
import {SocialTopic, strip_emojis} from "/case/app/social.js";

const n_name = store.lookup("name");
const n_description = store.lookup("description");
const n_media = store.lookup("media");

export default class LinkTreePlugin {
  process(action, query, context) {
    let url = new URL(query);
    let m = url.pathname.match(/^\/([A-Za-z0-9\._]+)/);
    if (!m) return;
    let username = m[1];
    if (!username) return;

    console.log("linktree search for", username);
    return {
      ref: username,
      name: username,
      description: "Linktree user",
      context: context,
      onitem: item => this.select(item),
    };
  }

  async select(item) {
    // Retrieve linktree profile for user.
    let username = item.ref;
    let igurl = item.context.proxy(`https://linktr.ee/${username}`);
    let r = await fetch(igurl);
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");
    console.log(doc);

    // Get JSON data block.
    let json = JSON.parse(doc.getElementById("__NEXT_DATA__").innerText);
    console.log("json", json);
    let account = json.props.pageProps.account;

    // Create new topic.
    let topic = item.context.new_topic();
    if (!topic) return;
    let name = account.pageTitle;
    if (!name) name = account.username;
    if (name) topic.add(n_name, name);
    if (account.description) topic.add(n_description, account.description);

    // Add links.
    let social = new SocialTopic(topic);
    for (let link of account.socialLinks) {
      social.add_link(link.url);
    }
    for (let link of account.links) {
      if (link.url) {
        social.add_link(link.url, strip_emojis(link.title));
      }
    }

    // Add profile photo.
    if (account.profilePictureUrl) {
      topic.add(n_media, account.profilePictureUrl);
    }

    // Update topic list.
    await item.context.editor.update_topics();
    await item.context.editor.navigate_to(topic);
  }
};


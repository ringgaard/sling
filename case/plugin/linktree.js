// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from LinkTree profile.

import {store} from "/case/app/global.js";
import {SocialTopic, strip_emojis} from "/case/app/social.js";

const n_name = store.lookup("name");
const n_description = store.lookup("description");
const n_media = store.lookup("media");

export default class LinkTreePlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let m = url.pathname.match(/^\/([A-Za-z0-9\._]+)/);
    if (!m) return;
    let username = m[1];
    if (!username) return;

    if (action == 1) { // SEARCHURL
      console.log("linktree search for", username);
      return {
        ref: username,
        name: username,
        description: "Linktree user",
        context: context,
        onitem: item => this.select(item),
      };
    } else if (action == 3) { // PASTEURL
      await this.populate(context, context.topic, username);
      return true;
    }
  }

  async select(item) {
    // Create new topic.
    let topic = item.context.new_topic();
    if (!topic) return;

    // Fetch profile from linktree and populate topic.
    await this.populate(item.context, topic, item.ref);

    // Update topic list.
    await item.context.editor.update_topics();
    await item.context.editor.navigate_to(topic);
  }

  async populate(context, topic, username) {
    // Retrieve linktree profile for user.
    let r = await fetch(context.proxy(`https://linktr.ee/${username}`));
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");
    console.log(doc);

    // Get JSON data block.
    let json = JSON.parse(doc.getElementById("__NEXT_DATA__").innerText);
    console.log("json", json);
    let account = json.props.pageProps.account;

    // Add name and description.
    let name = account.pageTitle;
    if (!name) name = account.username;
    if (name) topic.put(n_name, name);
    if (account.description) topic.put(n_description, account.description);

    // Add links.
    let social = new SocialTopic(topic, context);
    for (let link of account.socialLinks) {
      await social.add_link(link.url);
    }
    for (let link of account.links) {
      if (link.url) {
        await social.add_link(link.url, strip_emojis(link.title));
      }
    }

    // Add profile photo.
    if (account.profilePictureUrl) {
      topic.add(n_media, account.profilePictureUrl);
    }
  }
};


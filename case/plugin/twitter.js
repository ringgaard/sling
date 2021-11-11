// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from Twitter profile.

import {store, settings} from "/case/app/global.js";

const n_is = store.lookup("is");
const n_name = store.lookup("name");
const n_description = store.lookup("description");
const n_location = store.lookup("P276");
const n_media = store.lookup("media");
const n_twitter = store.lookup("P2002");
const n_start_time = store.lookup("P580");
const n_twitter_id = store.lookup("P6552");
const n_homepage = store.lookup("P856");

let xrefs = [
  {
    pattern: /^https?:\/\/[Ii]nstagram\.com\/([^\/\?]+)/,
    property: store.lookup("P2003"),
  },
  {
    pattern: /^https?:\/\/www\.instagram\.com\/([^\/\?]+)/,
    property: store.lookup("P2003"),
  },
  {
    pattern: /^https?:\/\/www\.facebook\.com\/([^\/\?]+)/,
    property: store.lookup("P2013"),
  },
  {
    pattern: /^https?:\/\/open\.spotify\.com\/artist\/([^\/\?]+)/,
    property: store.lookup("P1902"),
  },
  {
    pattern: /^https?:\/\/vk\.com\/([^\/\?]+)/,
    property: store.lookup("P3185"),
  },

  {
    pattern: /^https?:\/\/www\.twitch\.tv\/([^\/\?]+)/,
    property: store.lookup("P5797"),
  },
  {
    pattern: /^https?:\/\/[Oo]nlyfans\.com\/([^\/\?]+)/,
    property: store.lookup("P8604"),
  },
  {
    pattern: /^https?:\/\/www\.onlyfans\.com\/([^\/\?]+)/,
    property: store.lookup("P8604"),
  },
];

function str2date(s) {
  let d = new Date(s);
  let year = d.getFullYear();
  let month = d.getMonth() + 1;
  let day = d.getDate();
  return year * 10000 + month * 100 + day;
}

function strip_emojis(s) {
  return s.replace(/([\u2700-\u27BF]|[\uE000-\uF8FF]|\uD83C[\uDC00-\uDFFF]|\uD83D[\uDC00-\uDFFF]|[\u2011-\u26FF]|\uD83E[\uDD10-\uDDFF])/g, "").trim();
}

export default class TwitterPlugin {
  process(action, query, context) {
    let url = new URL(query);
    let username = url.pathname.substring(1);
    console.log("twitter search for", username);
    return {
      ref: username,
      name: username,
      description: "Twitter user",
      context: context,
      onitem: item => this.select(item),
    };
  }

  async select(item) {
    // Retrieve profile from twitter service.
    let qs = `user=${encodeURIComponent(item.ref)}`;
    let r = await fetch("/case/service/twitter?" + qs);
    let profile = await r.json();
    console.log("twitter profile", profile);

    // Create new topic.
    let topic = item.context.new_topic();
    if (!topic) return;

    // TODO: look up P2002/<username> in KB and add is: link.

    // Add information from twitter profile to topic.
    topic.add(n_name, strip_emojis(profile.name));
    if (profile.description) {
      topic.add(n_description, strip_emojis(profile.description));
    }
    if (profile.location) {
      // TODO: try to look up location.
      topic.add(n_location, strip_emojis(profile.location));
    }

    // Add twitter ID.
    let t = topic.store.frame();
    t.add(n_is, profile.screen_name);
    t.add(n_start_time, str2date(profile.created_at));
    //t.add(n_twitter_id, profile.id_str);
    topic.add(n_twitter, t);

    // Add cross references.
    for (let part of ["url", "description"]) {
      if (part in profile.entities) {
        for (let link of profile.entities[part].urls) {
          let url = link.expanded_url;
          console.log("link", url);

          // Try to match url to xref property.
          let prop = null;
          let identifier = null;
          for (let xref of xrefs) {
            let m = url.match(xref.pattern);
            if (m) {
              prop = xref.property;
              identifier = m[1];
              break;
            }
          }
          if (prop) {
            topic.add(prop, identifier);
          } else {
            topic.add(n_homepage, url);
          }
        }
      }
    }

    // TODO: Try to find ig/insta @xxx in description.

    // Add photo.
    let photo = profile.profile_image_url_https;
    if (photo) {
      let url = photo.split("_normal").join("");
      topic.add(n_media, url);
    }

    // Update topic list.
    await item.context.editor.update_topics();
    await item.context.editor.navigate_to(topic);
  }
};


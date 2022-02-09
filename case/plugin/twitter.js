// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from Twitter profile.

import {store} from "/case/app/global.js";
import {SocialTopic, strip_emojis} from "/case/app/social.js";
import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";

const n_is = store.lookup("is");
const n_name = store.lookup("name");
const n_description = store.lookup("description");
const n_location = store.lookup("P276");
const n_named_as = store.lookup("P1810");
const n_media = store.lookup("media");
const n_twitter = store.lookup("P2002");
const n_start_time = store.lookup("P580");
const n_twitter_id = store.lookup("P6552");
const n_gender = store.lookup("P21");
const n_female = store.lookup("Q6581072");
const n_male = store.lookup("Q6581097");
const n_instance_of = store.lookup("P31");
const n_human = store.lookup("Q5");
const n_instagram = store.lookup("P2003");

function str2date(s) {
  let d = new Date(s);
  let year = d.getFullYear();
  let month = d.getMonth() + 1;
  let day = d.getDate();
  return year * 10000 + month * 100 + day;
}

function strip_links(s) {
  return s.replace(/https:\/\/t.co\/\w+/g, "").trim();
}

export default class TwitterPlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let username = url.pathname.substring(1);
    if (!username) return;
    let slash = username.indexOf("/");
    if (slash != -1) s.substring(0, slash);

    if (action == SEARCHURL) {
      console.log("twitter search for", username);
      return {
        ref: username,
        name: username,
        description: "Twitter user",
        context: context,
        onitem: item => this.select(item),
      };
    } else if (action == PASTEURL) {
      await this.populate(context, context.topic, username);
      return true;
    }
  }

  async select(item) {
    // Create new topic.
    let topic = item.context.new_topic();
    if (!topic) return;

    // Fetch profile from twitter and populate topic.
    await this.populate(item.context, topic, item.ref);
  }

  async populate(context, topic, user) {
    // Retrieve profile from twitter service.
    let r = await fetch(context.service("twitter", {user}));
    let profile = await r.json();
    console.log("twitter profile", profile);

    // Check if user is already in knowledge base.
    let username = profile.screen_name;
    let item = await context.idlookup(n_twitter, username);
    if (item) topic.put(n_is, item);

    // Add twitter profile name and description to topic.
    topic.put(n_name, strip_emojis(profile.name));
    if (profile.description) {
      let description = strip_links(strip_emojis(profile.description));
      if (description) topic.put(n_description, description);

      // Get gender from pronouns.
      let gender = null;
      if (/she ?\/ ?her/i.test(description)) {
        gender = n_female;
      } else if (/he ?\/ ?him/i.test(description)) {
        gender = n_male;
      }
      if (gender) {
        topic.put(n_instance_of, n_human);
        topic.put(n_gender, gender);
      }
    }

    // Add location.
    let location = strip_emojis(profile.location);
    if (profile.location) {
      // Try to resolve location.
      r = await context.kblookup(location, {fullmatch: 1});
      let data = await r.json();
      if (data.matches.length > 0) {
        let id = data.matches[0].ref;
        let name = data.matches[0].text;
        let item = store.lookup(id);
        if (location.toLowerCase() == name.toLowerCase()) {
          topic.put(n_location, item);
        } else {
          let q = store.frame();
          q.add(n_is, item);
          q.add(n_named_as, location);
          topic.put(n_location, q);
        }
      } else {
        topic.put(n_location, location);
      }
    }

    // Add twitter ID.
    if (!topic.has(n_twitter, username)) {
      let t = topic.store.frame();
      t.add(n_is, username);
      t.add(n_start_time, str2date(profile.created_at));
      //t.add(n_twitter_id, profile.id_str);
      topic.add(n_twitter, t);
    }

    // Add cross references.
    let social = new SocialTopic(topic, context);
    for (let part of ["url", "description"]) {
      if (part in profile.entities) {
        for (let link of profile.entities[part].urls) {
          let url = link.expanded_url;
          if (!url) url = link.url;
          if (!url) continue;
          console.log("link", url);
          await social.add_link(url);
        }
      }
    }

    // Try to find IG address in description.
    if (!topic.has(n_instagram)) {
      let d = profile.description;
      let m = d.match(/ (instagram|ig|insta):? +@?([A-Za-z0-9_\.]+)/i);
      if (m) {
        let igname = m[2];
        topic.put(n_instagram, igname);
        let item = await context.idlookup(n_instagram, igname);
        if (item) topic.put(n_is, item);
      }
    }

    // Add photo.
    let photo = profile.profile_image_url_https;
    if (photo && !profile.default_profile_image) {
      let url = photo.split("_normal").join("");
      topic.put(n_media, url);
    }

    context.updated(topic);
  }
};


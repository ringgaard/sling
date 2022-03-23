// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from r/BeautifulFemales.

import {store} from "/case/app/global.js";
import {match_link} from "/case/app/social.js";
import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";

const n_is = store.lookup("is");
const n_name = store.lookup("name");
const n_media = store.lookup("media");
const n_gender = store.lookup("P21");
const n_female = store.lookup("Q6581072");
const n_instance_of = store.lookup("P31");
const n_human = store.lookup("Q5");
const n_date_of_birth = store.lookup("P569");
const n_subreddit = store.lookup("P3984");
const n_homepage = store.lookup("P856");

let linkbots = new Set([
  "SocialMediaMonkey",
  "bldrnr222",
  "AutoModerator",
]);

function titlecase(s) {
  let words = s.split(' ');
  let captal = words.map(word => word.charAt(0).toUpperCase() + word.slice(1));
  return captal.join(' ');
}

export default class BeautifulFemalesPlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let m = url.pathname.match(/^\/r\/\w+\/\w+\/[a-z0-9]+\/([^\/]+)/);
    if (!m) return;
    let title = decodeURIComponent(m[1]);
    if (title.endsWith("_irtr")) title = title.substring(0, title.length - 5);
    title = title.replace(/_/g, ' ');
    title = titlecase(title);

    if (action == SEARCHURL) {
      return {
        ref: query,
        name: title,
        description: "beautiful female",
        context: context,
        onitem: item => this.select(item),
      };
    } else if (action == PASTEURL) {
      await this.populate(context, context.topic, query);
      return true;
    }
  }

  async select(item) {
    // Create new topic.
    let topic = await item.context.new_topic();
    if (!topic) return;

    // Fetch profile from r/BeautifulFemales and populate new topic.
    await this.populate(item.context, topic, item.ref);
  }

  async populate(context, topic, url) {
    // Get post from Reddit.
    let r = await fetch(context.proxy(url + ".json"), {headers: {
      "XUser-Agent": navigator.userAgent,
    }});
    let reply = await r.json();

    // Get name.
    let posting = reply[0].data.children[0].data;
    let title = posting.title;
    let pos = title.indexOf('[');
    if (pos == -1) pos = title.indexOf('(');
    if (pos != -1) title = title.substring(0, pos);
    topic.put(n_name, title.trim());

    // All topics are females.
    topic.put(n_instance_of, n_human);
    topic.put(n_gender, n_female);

    // Get age from flair text.
    if (posting.link_flair_text) {
      let m = posting.link_flair_text.match(/Age: (\d+)/);
      if (m) {
        let age = parseInt(m[1]);
        let now = new Date(posting.created_utc * 1000);
        let dob = now.getFullYear() - age;
        topic.put(n_date_of_birth, dob);
      }
    }


    // Look for comments from linkbots.
    if (reply.length >= 1) {
      for (let child of reply[1].data.children) {
        let comment = child.data;
        if (linkbots.has(comment.author)) {
          let body = comment.body;

          // Get age.
          let m = body.match(/\(Age: (\d+)\)/);
          if (m) {
            let age = parseInt(m[1]);
            let now = new Date(comment.created_utc * 1000);
            let dob = now.getFullYear() - age;
            topic.put(n_date_of_birth, dob);
          }

          // Add social media links.
          for (let match of body.matchAll(/\]\((htt[^\)]+)\)/g)) {
            let url = match[1];
            let [prop, identifier] = match_link(url);
            if (prop) {
              topic.put(prop, identifier);

              // Check for matching topic in knowledge base.
              let item = await context.idlookup(prop, identifier);
              if (item) topic.put(n_is, item);
            } else {
              console.log("unmatched", url);
            }
          }

          // Get subreddit(s).
          for (let match of body.matchAll(/^\/?r\/([a-zA-Z0-9-_]+)\s+$/mg)) {
            let subreddit = match[1];
            topic.put(n_subreddit, subreddit);
          }

          // Get homepage.
          for (let match of body.matchAll(/^(https?:\/\/.+)$/mg)) {
            topic.put(n_homepage, match[1]);
          }
        }
      }
    }

    // Add image(s).
    if (posting.url) {
      let r = await fetch(context.service("albums", {url: posting.url}));
      let profile = await store.parse(r);
      for (let media of profile.all(n_media)) {
        topic.add(n_media, media);
      }
    }

    context.updated(topic);
  }
};


// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding topic from Twitter profile.

import {store, settings} from "/case/app/global.js";

const n_is = store.lookup("is");
const n_name = store.lookup("name");
const n_description = store.lookup("description");
const n_location = store.lookup("P276");
const n_named_as = store.lookup("P1810");
const n_media = store.lookup("media");
const n_twitter = store.lookup("P2002");
const n_start_time = store.lookup("P580");
const n_twitter_id = store.lookup("P6552");
const n_homepage = store.lookup("P856");
const n_gender = store.lookup("P21");
const n_female = store.lookup("Q6581072");
const n_male = store.lookup("Q6581097");
const n_instance_of = store.lookup("P31");
const n_human = store.lookup("Q5");
const n_instagram = store.lookup("P2003");

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
    pattern: /^https?:\/\/facebook\.com\/([^\/\?]+)/,
    property: store.lookup("P2013"),
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
    pattern: /^https?:\/\/twitch\.tv\/([^\/\?]+)/,
    property: store.lookup("P5797"),
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
  {
    pattern: /^https?:\/\/discord\.gg\/([^\/\?]+)/,
    property: store.lookup("P9101"),
  },
  {
    pattern: /^https?:\/\/linkedin\.com\/in\/([^\/\?]+)/,
    property: store.lookup("P6634"),
  },
  {
    pattern: /^https?:\/\/\w+\.linkedin\.com\/in\/([^\/\?]+)/,
    property: store.lookup("P6634"),
  },
  {
    pattern: /^https?:\/\/linkedin\.com\/company\/([^\/\?]+)/,
    property: store.lookup("P4264"),
  },
  {
    pattern: /^https?:\/\/\w+\.linkedin\.com\/company\/([^\/\?]+)/,
    property: store.lookup("P4264"),
  },
  {
    pattern: /^https?:\/\/t\.me\/([^\/\?]+)/,
    property: store.lookup("P3789"),
  },
  {
    pattern: /^https?:\/\/vimeo\.com\/([^\/\?]+)/,
    property: store.lookup("P4015"),
  },
  {
    pattern: /^https?:\/\/www\.patreon\.com\/([^\/\?]+)/,
    property: store.lookup("P4175"),
  },
  {
    pattern: /^https?:\/\/youtube\.com\/channel\/([^\/\?]+)/,
    property: store.lookup("P2397"),
  },
  {
    pattern: /^https?:\/\/www\.youtube\.com\/channel\/([^\/\?]+)/,
    property: store.lookup("P2397"),
  },
  {
    pattern: /^https?:\/\/imdb\.com\/name\/([^\/\?]+)/,
    property: store.lookup("P345"),
  },
  {
    pattern: /^https?:\/\/m\.imdb\.com\/name\/([^\/\?]+)/,
    property: store.lookup("P345"),
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

function strip_links(s) {
  return s.replace(/https:\/\/t.co\/\w+/g, "").trim();
}

export default class TwitterPlugin {
  process(action, query, context) {
    let url = new URL(query);
    let username = url.pathname.substring(1);
    if (!username) return;

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
    let r = await fetch(item.context.service("twitter", {user: item.ref}));
    let profile = await r.json();
    console.log("twitter profile", profile);

    // Create new topic.
    let topic = item.context.new_topic();
    if (!topic) return;

    // Check if user is already in knowledge base.
    let username = profile.screen_name;
    r = await item.context.kblookup(`P2002/${username}`, {fullmatch: 1});
    let data = await r.json();
    if (data.matches.length == 1) {
      topic.add(n_is, store.lookup(data.matches[0].ref));
    }

    // Add twitter profile name and description to topic.
    topic.add(n_name, strip_emojis(profile.name));
    if (profile.description) {
      let description = strip_links(strip_emojis(profile.description));
      topic.add(n_description, description);

      // Get gender from pronouns.
      let gender = null;
      if (/she ?\/ ?her/i.test(description)) {
        gender = n_female;
      } else if (/he ?\/ ?him/i.test(description)) {
        gender = n_male;
      }
      if (gender) {
        topic.add(n_instance_of, n_human);
        topic.add(n_gender, gender);
      }
    }

    // Add location.
    let location = strip_emojis(profile.location);
    if (profile.location) {
      // Try to resolve location.
      r = await item.context.kblookup(location, {fullmatch: 1});
      let data = await r.json();
      if (data.matches.length > 0) {
        let id = data.matches[0].ref;
        let name = data.matches[0].text;
        let item = store.lookup(id);
        if (location.toLowerCase() == name.toLowerCase()) {
          topic.add(n_location, item);
        } else {
          let q = store.frame();
          q.add(n_is, item);
          q.add(n_named_as, location);
          topic.add(n_location, q);
        }
      } else {
        topic.add(n_location, location);
      }
    }

    // Add twitter ID.
    let t = topic.store.frame();
    t.add(n_is, username);
    t.add(n_start_time, str2date(profile.created_at));
    //t.add(n_twitter_id, profile.id_str);
    topic.add(n_twitter, t);

    // Add cross references.
    for (let part of ["url", "description"]) {
      if (part in profile.entities) {
        for (let link of profile.entities[part].urls) {
          let url = link.expanded_url;
          if (!url) url = link.url;
          if (!url) continue;
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
            if (!topic.has(prop, identifier)) {
              topic.add(prop, identifier);
            }
          } else {
            if (!topic.has(n_homepage, url)) {
              topic.add(n_homepage, url);
            }
          }
        }
      }
    }

    // Try to find IG address in description.
    if (!topic.has(n_instagram)) {
      let d = profile.description;
      let m = d.match(/ (instagram|ig|insta):? +@?([A-Za-z0-9_\.]+)/i);
      if (m) {
        let igname = m[2];
        topic.add(n_instagram, igname);
      }
    }

    // Add photo.
    let photo = profile.profile_image_url_https;
    if (photo && !profile.default_profile_image) {
      let url = photo.split("_normal").join("");
      topic.add(n_media, url);
    }

    // Update topic list.
    await item.context.editor.update_topics();
    await item.context.editor.navigate_to(topic);
  }
};


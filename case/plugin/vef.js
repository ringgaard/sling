// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for VEF postings.

import {store, frame} from "/common/lib/global.js";
import {MD5} from "/common/lib/hash.js";

const n_is = store.is;
const n_name = frame("name");
const n_description = frame("description");
const n_media = frame("media");
const n_instance_of = frame("P31");
const n_human = frame("Q5");
const n_gender = frame("P21");
const n_female = frame("Q6581072");
const n_date_of_birth = frame("P569");
const n_country_of_citizenship = frame("P27");
const n_denmark = frame("Q35");
const n_featured_in = frame("PFEIN");
const n_page9 = frame("t/1215/18");
const n_point_in_time = frame("P585");
const n_described_at_url = frame("P973");

const postpat = /http:\/\/vintage-erotica-forum\.com\/showpost.php\?p=(\d+)/
const p9 = true;

export default class VEFPlugin {
  async process(action, query, context) {
    let url = new URL(query);
    console.log(url.search);
    let m = url.search.match(/^\?p=(\d+)/);
    if (!m) return;
    let postingid = m[1];
    if (!postingid) return;

    return {
      ref: postingid,
      name: postingid,
      description: "VEF posting",
      url: query,
      context: context,
      onitem: item => this.extract(item),
    };
  }

  async extract(item) {
    // Retrieve VEF posting.
    let context = item.context;
    let r = await context.fetch(item.url);
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");
    let container = doc.querySelector("td.alt1");
    let parts = container.querySelectorAll("div");
    let title = parts.item(0).innerText.trim();
    let body = parts.item(1);

    // Get month from title.
    let date = new Date(title);
    let year = date.getFullYear();
    let month = date.getMonth() + 1;

    // Extract parts from posts.
    let day = 0;
    let text = "";
    let brk = false;
    let name = null;
    let age = null;
    let source = null;
    let topic = null;
    for (let e of body.childNodes) {
      if (e.nodeType == Node.TEXT_NODE) {
        if (brk) text = text.trim() + "\n";
        brk = false;
        text += e.textContent;
        name = null;
        age = null;
        topic = null;
      } else if (e.nodeType == Node.ELEMENT_NODE) {
        if (e.tagName == "BR") {
          brk = true;
        } else if (e.tagName == "B") {
          if (brk) text = text.trim() + "\n";
          brk = false;
          text += e.innerText;
        } else if (e.tagName == "A") {
          if (e.href.startsWith("https://www.imagevenue.com/")) {
            // Continuation.
            let m = text.match(/^,?\s*and\s+/);
            if (m) text = text.slice(m[0].length);

            // Track date.
            for (;;) {
              let m = text.match(/^(\d+)(st|nd|rd|th)\s*,?/)
              if (!m) break;
              day = parseInt(m[1]);
              text = text.slice(m[0].length).trim()
            }
            text = text.trim();
            let date = year * 10000 + month * 100 + day;

            // Parse name and age.
            m = text.match(/^([A-Za-z\- ]+)\s*(\(\d+\))?/)
            if (m) {
              name = m[1];
              age = m[2] && parseInt(m[2].slice(1, m[2].length - 1));
              text = text.slice(m[0].length).trim();
            }
            if (text.startsWith("(IM)")) text = text.slice(4).trim();
            if (text.startsWith("(M)")) text = text.slice(3).trim();

            // Create new topic.
            if (!topic) {
              topic = await context.new_topic();
              if (name) topic.put(n_name, name);
              if (p9) topic.put(n_description, "Side 9 pige");
              if (text) topic.put(n_description, text);
              topic.put(n_instance_of, n_human);
              topic.put(n_gender, n_female);
              if (age) topic.put(n_date_of_birth, year - age);
              if (p9) topic.put(n_country_of_citizenship, n_denmark);
              if (p9) {
                let f = store.frame();
                f.add(n_is, n_page9);
                f.add(n_point_in_time, date);
                topic.put(n_featured_in, f);
              }
              if (source) topic.put(n_described_at_url, source);
              context.updated(topic);
            }

            // Add photo.
            let img = e.querySelector("img");
            if (img) {
              let thumb = img.src;
              let base = thumb.slice(thumb.lastIndexOf("/") + 1);
              let hires = base.replace("_t", "_o");
              let d = MD5(hires);
              let path = `${d[0]}${d[1]}/${d[2]}${d[3]}/${d[4]}${d[5]}`
              let photo = "https://cdn-images.imagevenue.com/" +
                          path + "/" + hires;
              console.log("link", date, name, age, text, photo);
              topic.add(n_media, "!" + photo);
            }

            text = "";
            brk = false;
            source = null;
          } else {
            if (e.href.match(postpat)) source = e.href;
            text += e.innerText;
          }
        } else {
          console.log("element", e);
        }
      }
    }
  }
}


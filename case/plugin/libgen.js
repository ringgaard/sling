// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for adding e-books from Library Genesis.

import {store, frame} from "/case/app/global.js";
import {inform} from "/common/lib/material.js";
import {SEARCHURL, PASTEURL} from "/case/app/plugins.js";
import {date_parser} from "/case/app/value.js";
import {Drive} from "/case/app/drive.js";

const n_name = frame("name");
const n_description = frame("description");
const n_subtitle = frame("P1680");
const n_media = frame("media");
const n_instance_of = frame("P31");
const n_book = frame("Q571");
const n_author = frame("P50");
const n_publisher = frame("P123");
const n_published = frame("P577");
const n_isbn10 = frame("P957");
const n_isbn13 = frame("P212");
const n_libgen = frame("PLIBG");
const n_full_work_url = frame("P953");

export default class LibgenPlugin {
  async process(action, query, context) {
    let url = new URL(query);
    let m = url.pathname.match(/^\/main\/(.+)/);
    if (!m && url.pathname == "/book/index.php") {
      m = url.search.match(/\\?md5=(.+)/);
    }
    if (!m) return;
    let md5 = m[1];
    if (!md5) return;

    if (action == SEARCHURL) {
      return {
        ref: md5,
        name: md5,
        description: "E-book",
        context: context,
        onitem: item => this.select(item),
      };
    } else if (action == PASTEURL) {
      await this.populate(context, context.topic, md5);
      return true;
    }
  }

  async select(item) {
    // Create new topic.
    let topic = await item.context.new_topic();
    if (!topic) return;

    // Fetch e-book and populate topic.
    await this.populate(item.context, topic, item.ref);
  }

  async populate(context, topic, md5) {
    // Retrieve meta information for e-book.
    let r = await context.fetch(`http://library.lol/main/${md5}`);
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");
    let info = doc.getElementById("info");

    // Get book metadata.
    var authors, publisher, year, isbns, cover, summary, subtitle;

    let title = info.querySelector("h1").innerText.trim();
    let split = title.indexOf(":");
    if (split == -1) split = title.indexOf(";");
    if (split != -1) {
      subtitle = title.slice(split + 1).trim();
      title = title.slice(0, split).trim();
    }

    let img = info.querySelector("img");
    if (img) {
      let src = img.getAttribute("src");
      if (src != "/img/blank.png") cover = "http://library.lol" + src;
    }

    for (let p of info.querySelectorAll("p")) {
      let text = p.innerText;
      if (text.startsWith("Author(s): ")) {
        authors = new Array();
        for (let name of text.slice(11).split(";")) {
          let comma = name.indexOf(",");
          if (comma != -1) {
            let lastname = name.slice(comma + 1).trim();
            let firstname = name.slice(0, comma).trim();
            name = firstname + " " + lastname;
          }
          authors.push(name.trim());
        }
      } else if (text.startsWith("Publisher: ")) {
        publisher = text.slice(11);
        let m = publisher.match(/(.+), Year: (\d+)/);
        if (m) {
          publisher = m[1];
          year = parseInt(m[2]);
        }
      } else if (text.startsWith("ISBN: ")) {
        isbns = text.slice(6).split(",");
      }
    }
    for (let p of info.querySelectorAll("div")) {
      let text = p.innerText;
      if (text.startsWith("Description:")) {
        summary = text.slice(12).trim();
      }
    }
    let url = decodeURI(info.querySelector("#download h2 a").href);

    // Add book information to topic.
    topic.put(n_name, title);
    topic.put(n_subtitle, subtitle);
    topic.put(n_instance_of, n_book);
    if (authors) {
      for (let author of authors) {
        topic.put(n_author, author);
      }
    }
    topic.put(n_publisher, publisher);
    topic.put(n_published, year);
    if (summary) {
      topic.add(null, summary);
    }
    topic.put(n_full_work_url, url);
    if (isbns) {
      for (let isbn of isbns) {
        topic.put(isbn.length < 13 ? n_isbn10 : n_isbn13, isbn);
      }
    }
    topic.put(n_libgen, md5);
    topic.put(n_media, cover);

    // Download book in background.
    let ext = url.split('.').pop();
    let filename = title + "." + ext;
    this.download(context, topic, url, filename);

    context.updated(topic);
  }

  async download(context, topic, url, filename) {
    let r = await context.fetch(url);
    let content = await r.blob();

    // Save book to drive.
    let file = new File([content], filename);
    let driveurl = await Drive.save(file);

    // Update topic.
    topic.set(n_full_work_url, driveurl);
    context.update(topic);

    inform(`Book downloaded: ${filename}`);
  }
}


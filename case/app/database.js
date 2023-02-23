// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Local case database.

import {Store, Encoder, Decoder} from "/common/lib/frame.js";
import {store, frame} from "./global.js";

const n_name = frame("name");
const n_description = frame("description");
const n_main = frame("main");
const n_caseid = frame("caseid");
const n_created = frame("created");
const n_modified = frame("modified");
const n_shared = frame("shared");
const n_topics = frame("topics");
const n_share = frame("share");
const n_publish = frame("publish");
const n_collaborate = frame("collaborate");
const n_secret = frame("secret");
const n_link = frame("link");
const n_has_quality = frame("P1552");
const n_not_safe_for_work = frame("Q2716583");

class CaseDatabase {
  // Open database.
  open() {
    return new Promise((resolve, reject) => {
      // Check if database has already been opened.
      if (this.db) resolve(this);

      // Open database.
      let request = window.indexedDB.open("Case", 1);
      request.onerror = e => {
        reject(this);
      };

      // Create database if needed.
      request.onupgradeneeded = e => {
        console.log("Database upgrade");
        let db = e.target.result;

        // Create case directory.
        let casedir = db.createObjectStore("casedir", { keyPath: "id" });
        casedir.transaction.oncomplete = e => {
          console.log("Case directory created");
        };

        // Create case data store.
        let casedata = db.createObjectStore("casedata", { keyPath: "id" });
        casedata.transaction.oncomplete = e => {
          console.log("Case data store created");
        };
      }

      // Store database connection and install global error handler on success.
      request.onsuccess = e => {
        this.db = e.target.result;
        this.db.onerror = e => this.onerror(e);
        resolve(this);
      };
    });
  }

  onerror(e) {
    console.log("Database error", e.target.error);
  }

  read(caseid) {
    return new Promise((resolve, reject) => {
      let tx = this.db.transaction(["casedata"]);
      let casedata = tx.objectStore("casedata");
      let request = casedata.get(caseid);
      request.onsuccess = e => {
        let rec = e.target.result;
        if (rec) {
          let decoder = new Decoder(store, rec.data);
          let casefile = decoder.readall();
          resolve(casefile);
        } else {
          resolve(null);
        }
      }
      request.onerror = e => {
        reject(e);
      }
    });
  }

  async writemeta(casefile) {
    function date(ts) {
      return ts ? new Date(ts) : null;
    }

    // Build case directory record.
    let caseid = casefile.get(n_caseid);
    let main = casefile.get(n_main);
    let rec = {
      id: caseid,
      name: main.get(n_name),
      description: main.get(n_description),
      created: date(casefile.get(n_created)),
      modified: date(casefile.get(n_modified)),
      shared: date(casefile.get(n_shared)),
      share: !!casefile.get(n_share),
      publish: !!casefile.get(n_publish),
      collaborate: !!casefile.get(n_collaborate),
      secret: casefile.get(n_secret),
      link: !!casefile.get(n_link),
      nsfw: main.has(n_has_quality, n_not_safe_for_work),
    };

    // Write record to database.
    await this.writerec("casedir", rec);

    return rec;
  }

  async write(casefile) {
    // Encode case data.
    let encoder = new Encoder(store);
    if (casefile.has(n_topics)) {
      for (let topic of casefile.get(n_topics)) {
        encoder.encode(topic);
      }
    }
    encoder.encode(casefile);
    let data = {id: casefile.get(n_caseid), data: encoder.output()};

    // Write case data.
    await this.writerec("casedata", data);

    // Write case metadata to case directory.
    return await this.writemeta(casefile);
  }

  async remove(caseid, link) {
    // Remove case from directory.
    await this.removerec("casedir", caseid);

    if (!link) {
      // Remove case from data store.
      await this.removerec("casedata", caseid);
    }
  }

  readdir() {
    return this.readall("casedir");
  }

  readall(table) {
    return new Promise((resolve, reject) => {
      // Read case directory and add to list.
      let list = new Array();
      let objstore = this.db.transaction(table).objectStore(table);
      objstore.openCursor().onsuccess = e => {
        var cursor = e.target.result;
        if (cursor) {
          list.push(cursor.value);
          cursor.continue();
        } else {
          resolve(list);
        }
      };
    });
  }

  writerec(table, rec) {
    return new Promise((resolve, reject) => {
      let objstore = this.db.transaction(table, "readwrite").objectStore(table);
      let req = objstore.put(rec);
      req.onsuccess = e => { resolve(e); };
      req.onerror = e => { reject(e); };
    });
  }

  async writeall(table, data) {
    for (let rec of data) {
      await this.writerec(table, rec);
    }
  }

  removerec(table, id) {
    return new Promise((resolve, reject) => {
      let objstore = this.db.transaction(table, "readwrite").objectStore(table);
      let req = objstore.delete(id);
      req.onsuccess = e => { resolve(e); };
      req.onerror = e => { reject(e); };
    });
  }
}

export var casedb = new CaseDatabase();

